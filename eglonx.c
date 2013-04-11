//
//  Created by exoticorn (http://talk.maemo.org/showthread.php?t=37356)
//  edited and commented by André Bergner [endboss]
//
//  libraries needed: -lEGL -lGLESv2 -lX11
//
//
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/time.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext_brcm.h>

typedef struct Rect {
    float orgx;
    float orgy;
    float sizex;
    float sizey;
} Rect;

//// initial window size
#define WINDOW_WIDTH    (800)
#define WINDOW_HEIGHT   (480)

//// X windows globals
static Display *Xdsp;
static Window Xwin;
static XWindowAttributes Xgwa;
static GC Xgc; 
static XImage *Ximage = 0;
static Rect Xwindowrect;

//// EGL globals
static EGLDisplay egl_display;
static EGLContext egl_context;
static EGLSurface egl_surface;

//// user supplied funcs
void client_motionevent(int posx, int posy);
void client_mouseunclickevent(int posx, int posy, int button);
void client_mouseclickevent(int posx, int posy, int button);
void client_keypressevent(int posx, int posy, int key);
void client_exposeevent();
void client_render();
void client_glinit();


//// signal interrupt handler

static bool sigkeeprunning = true;

void sighandler(int dummy) 
{
    sigkeeprunning = false;
}

void signalinit()
{
    signal(SIGINT, sighandler);
    signal(SIGQUIT, sighandler);
}


//// Rect support

#define RectGetMinX(r) ((r).orgx)
#define RectGetMinY(r) ((r).orgy)
#define RectGetMaxX(r) ((r).orgx + (r).sizex)
#define RectGetMaxY(r) ((r).orgy + (r).sizey)
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#define MAX(a, b)       (((a) > (b)) ? (a) : (b))

Rect RectMake(float ox, float oy, float width, float height)
{
    Rect r;

    r.orgx = ox;
    r.orgy = oy;
    r.sizex = width;
    r.sizey = height;
    return r;
}

Rect RectNull()
{
    return RectMake(-1000, -1000, -1000, -1000);
}

int RectIsNull(Rect r)
{
    if(r.orgx != -1000)
        return 0;
    if(r.orgy != -1000)
        return 0;
    if(r.sizex != -1000)
        return 0;
    if(r.sizey != -1000)
        return 0;
    return 1;
}

Rect RectIntersection(Rect a, Rect b)
{
    float minx, maxx, miny, maxy;

    if(RectIsNull(a)) return RectNull();
    if(RectIsNull(b)) return RectNull();
    if(RectGetMinX(a) > RectGetMaxX(b)) return RectNull();
    if(RectGetMinY(a) > RectGetMaxY(b)) return RectNull();
    if(RectGetMinX(b) > RectGetMaxX(a)) return RectNull();
    if(RectGetMinY(b) > RectGetMaxY(a)) return RectNull();
    minx = MAX(RectGetMinX(a),RectGetMinX(b));
    maxx = MIN(RectGetMaxX(a),RectGetMaxX(b));
    miny = MAX(RectGetMinY(a),RectGetMinY(b));
    maxy = MIN(RectGetMaxY(a),RectGetMaxY(b));
    return RectMake(minx, miny, maxx-minx, maxy-miny);
}

Rect RectEvenWidth(Rect r)
{
    int sizex = r.sizex;
    if(sizex & 1) {
        if(r.orgx>0.0)
            r.orgx -= 1.0;
        r.sizex += 1.0;
    }
    return r;
}

//// X window support 

void xfixedsize(Display *dsp, Window win, int sizex, int sizey)
{
    XSizeHints *hints = XAllocSizeHints();
    hints->flags = PMinSize | PMaxSize;
    hints->min_width = sizex;
    hints->max_width = sizex;
    hints->min_height = sizey;
    hints->max_height = sizey;
    XSetWMNormalHints(dsp, win, hints);
}

void xwindowsinit()
{
    Xdsp = XOpenDisplay(NULL);
    if (Xdsp == NULL) {
        fputs("cannot connect to X server\n", stderr);
        return;
    }
    Window root = DefaultRootWindow(Xdsp);

    XSetWindowAttributes swa;
    swa.event_mask = ExposureMask | 
                     KeyPressMask | 
                     KeyReleaseMask | 
                     PointerMotionMask | 
                     ButtonMotionMask | 
                     ButtonPressMask | 
                     ButtonReleaseMask;
    Xwin = XCreateWindow(Xdsp, root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, CopyFromParent, 
                                InputOutput, CopyFromParent, CWEventMask, &swa);
    XMapWindow(Xdsp, Xwin); // make the window visible on the screen
    XStoreName(Xdsp, Xwin, "GL test"); // give the window a name
    xfixedsize(Xdsp, Xwin, WINDOW_WIDTH, WINDOW_HEIGHT); // force fixed window size

    //// create an X image for drawing to the screen
    Xgc = DefaultGC(Xdsp, 0);
    XGetWindowAttributes(Xdsp, Xwin, &Xgwa);
    char *buf = (char *)malloc(Xgwa.width*Xgwa.height*2);
    Ximage = XCreateImage(Xdsp, 
                DefaultVisual(Xdsp, DefaultScreen(Xdsp)),
                DefaultDepth(Xdsp, DefaultScreen(Xdsp)),
                ZPixmap, 0, buf, Xgwa.width, Xgwa.height, 16, 0);
    Xwindowrect = RectMake(0,0,Xgwa.width,Xgwa.height);
}

void xwindowscleanup()
{
    XDestroyWindow(Xdsp, Xwin);
    XCloseDisplay(Xdsp);
}

void xdisplayGLbuffer(Rect copyrect)	// copy this rectangleto the X window
{
    static unsigned int *pixbuffer;
    static int pixbufferbytes;
    int orgx, orgy, sizex, sizey;
    int winsizex, winsizey;

    copyrect = RectIntersection(Xwindowrect, copyrect);
    if(RectIsNull(copyrect))
	return;
    copyrect = RectEvenWidth(copyrect);	// force copy rect to be even
    winsizex = Xwindowrect.sizex;
    winsizey = Xwindowrect.sizey;
    if(winsizex & 1) {
	fprintf(stderr, "Error: window size x must be even\n");
	return;
    }

    int nbytes = winsizex * winsizey * 4;
    if(pixbufferbytes != nbytes) {
        if(pixbuffer)
            free(pixbuffer);
        pixbuffer = (unsigned int *)malloc(nbytes);
        pixbufferbytes = nbytes;
    }
    glFinish();
    glReadPixels(copyrect.orgx, copyrect.orgy, copyrect.sizex, copyrect.sizey, 
						GL_RGBA, GL_UNSIGNED_BYTE, pixbuffer);
    unsigned int *pixptr = pixbuffer;
    int count, x, y;
    for(y=0; y<copyrect.sizey; y++) {
        int srcy = copyrect.sizey-1-y;		// flip y for X windows
        unsigned int *dest = ((unsigned int*)(&(Ximage->data[0])))+(srcy*(winsizex/2));
        count = copyrect.sizex/2;
        while(count--) {
            unsigned int src0 = pixptr[0];
            unsigned int src1 = pixptr[1];
            pixptr += 2;

            *dest++ = ((src1 & 0xf8)      <<24) |
                      ((src1 & (0xfc<< 8))<<11) |
                      ((src1 & (0xf8<<16))>> 3) |
                      ((src0 & 0xf8)      << 8) |
                      ((src0 & (0xfc<< 8))>> 5) |
                      ((src0 & (0xf8<<16))>>19);
        }
    }
    orgx = copyrect.orgx;
    orgy = winsizey-(copyrect.orgy+copyrect.sizey);
    sizex = copyrect.sizex;
    sizey = copyrect.sizey;
    XPutImage(Xdsp, Xwin, Xgc, Ximage, 0, 0, orgx, orgy, sizex, sizey);
}

int xgetevents()
{
    int havemotion;
    int motionx, motiony;

    havemotion = 0;
    while (XPending(Xdsp)) { // check for events from the x-server
        XEvent xev;
        XNextEvent(Xdsp, &xev);
        switch(xev.type) {
            case Expose:
                if(xev.xexpose.count == 0)
                    client_exposeevent();
                break;
            case MotionNotify:
                motionx = xev.xmotion.x;
                motiony = WINDOW_HEIGHT-xev.xmotion.y;
                havemotion = 1;
                break;
            case ButtonPress:
                if(havemotion) {
                    client_motionevent(motionx, motiony);
                    havemotion = 0;
                }
                client_mouseclickevent(xev.xbutton.x, WINDOW_HEIGHT-xev.xbutton.y, xev.xbutton.button);
                break;
            case ButtonRelease:
                if(havemotion) {
                    client_motionevent(motionx, motiony);
                    havemotion = 0;
                }
                client_mouseunclickevent(xev.xbutton.x, WINDOW_HEIGHT-xev.xbutton.y, xev.xbutton.button);
                break;
            case KeyPress:
                client_keypressevent(xev.xkey.x, WINDOW_HEIGHT-xev.xkey.y, xev.xkey.keycode);
                return 0;
        }
    }
    if(havemotion) {
        client_motionevent(motionx, motiony);
        havemotion = 0;
    }
    return 1;
}


//// EGL support
//
// egl provides an interface to connect the graphics related functionality of openGL ES
// with the windowing interface and functionality of the native operation system (X11
// in our case.
void eglinit()
{
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        fputs("Got no EGL display.\n", stderr);
        return;
    }
    if (!eglInitialize(egl_display, NULL, NULL)) {
        fputs("Unable to initialize EGL\n", stderr);
        return;
    }

    EGLint attr[] = { // some attributes to set up our egl-interface
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE,
        EGL_PIXMAP_BIT | EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint num_config;
    EGLConfig ecfg;

    if (!eglChooseConfig(egl_display, attr, &ecfg, 1, &num_config)) {
        fprintf(stderr, "Failed to choose config (eglError: %s)\n", eglGetError());
        return;
    }
    if (num_config != 1) {
        fprintf(stderr, "Didn't get exactly one config, but %d\n", num_config);
        return;
    }

    EGLint rt;
    EGLint pixel_format = EGL_PIXEL_FORMAT_ARGB_8888_BRCM;
    eglGetConfigAttrib(egl_display, ecfg, EGL_RENDERABLE_TYPE, &rt);
    if (rt & EGL_OPENGL_ES_BIT) {
        pixel_format |= EGL_PIXEL_FORMAT_RENDER_GLES_BRCM;
        pixel_format |= EGL_PIXEL_FORMAT_GLES_TEXTURE_BRCM;
    }
    if (rt & EGL_OPENGL_ES2_BIT) {
        pixel_format |= EGL_PIXEL_FORMAT_RENDER_GLES2_BRCM;
        pixel_format |= EGL_PIXEL_FORMAT_GLES2_TEXTURE_BRCM;
    }
    if (rt & EGL_OPENVG_BIT) {
        pixel_format |= EGL_PIXEL_FORMAT_RENDER_VG_BRCM;
        pixel_format |= EGL_PIXEL_FORMAT_VG_IMAGE_BRCM;
    }
    if (rt & EGL_OPENGL_BIT) {
        pixel_format |= EGL_PIXEL_FORMAT_RENDER_GL_BRCM;
    }

    EGLint pixmap[5];
    pixmap[0] = 0;
    pixmap[1] = 0;
    pixmap[2] = WINDOW_WIDTH;
    pixmap[3] = WINDOW_HEIGHT;
    pixmap[4] = pixel_format;
    eglCreateGlobalImageBRCM(WINDOW_WIDTH, WINDOW_HEIGHT, pixmap[4], 0, WINDOW_WIDTH*4, pixmap);
    egl_surface = eglCreatePixmapSurface(egl_display, ecfg, pixmap, 0);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Unable to create EGL surface (eglError: %s)\n", eglGetError());
        return;
    }

    //// egl-contexts collect all state descriptions needed required for operation
    EGLint ctxattr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, ecfg, EGL_NO_CONTEXT, ctxattr);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Unable to create EGL context (eglError: %s)\n", eglGetError());
        return;
    }

    //// associate the egl-context with the egl-surface
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

void eglcleanup()
{
    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
}


//// support for frames per second display

static struct timeval fpst1, fpst2;
static int fpsnframes = -1;

void _fpsinit()
{
    gettimeofday(&fpst1, 0);
    fpsnframes = 0;
}

int fpsreport()
{
    if(fpsnframes == -1)
        _fpsinit();
    fpsnframes++;
    if((fpsnframes % 100) == 0)
        return 1;
    else
        return 0;
}

float fpsget()
{
    gettimeofday(&fpst2, 0);
    float dt = (fpst2.tv_sec-fpst1.tv_sec) + ((fpst2.tv_usec-fpst1.tv_usec)*1e-6);
    float fps = fpsnframes / dt;
    fpsnframes = 0;
    fpst1 = fpst2;
    return fps;
}


//// client gl code follows

static const char vertex_src [] = "     \
    attribute vec4 position;    \
    varying mediump vec2 pos;   \
    uniform vec4 offset;        \
                                \
    void main()                 \
    {                           \
            gl_Position = position + offset;    \
            pos = position.xy;                  \
    }                                           \
";

static const char fragment_src [] = "   \
    varying mediump vec2 pos;   \
    uniform mediump float phase;\
                                \
    void main()                 \
    {                           \
            gl_FragColor = vec4(1., 0.9, 0.7, 1.0) *    \
            cos(30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y) \
            + atan(pos.y,pos.x) - phase);               \
    }                                                   \
";

// some more formulas to play with...
// cos(20.*(pos.x*pos.x + pos.y*pos.y) - phase);
// cos(20.*sqrt(pos.x*pos.x + pos.y*pos.y) + atan(pos.y,pos.x) - phase);
// cos(30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y - 1.8*pos.x*pos.y*pos.y)
// + atan(pos.y,pos.x) - phase);

void print_shader_info_log(GLuint shader)
{
    GLint length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length) {
        char* buffer = malloc(sizeof(char) * length);
        glGetShaderInfoLog(shader, length, NULL, buffer);
        //fprintf(stderr, "shader info: %s\n",buffer);
        fflush(NULL);
        free(buffer);
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) exit (1);
    }
}

GLuint load_shader(const char *shader_source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1 ,&shader_source , NULL);
    glCompileShader(shader);
    print_shader_info_log(shader);
    return shader;
}

static GLfloat norm_x = 0.0, norm_y = 0.0;
static GLfloat offset_x = 0.0, offset_y = 0.0;
static GLfloat p1_pos_x = 0.0, p1_pos_y = 0.0;
static GLint phase_loc, offset_loc, position_loc;

const float vertexArray[] = {
        0.0, 0.5, 0.0,
        -0.5, 0.0, 0.0,
        0.0, -0.5, 0.0,
        0.5, 0.0, 0.0,
        0.0, 0.5, 0.0 
};

void client_glinit()
{
    GLuint vertexShader = load_shader(vertex_src , GL_VERTEX_SHADER); // load vertex shader
    GLuint fragmentShader = load_shader(fragment_src , GL_FRAGMENT_SHADER); // load fragment shader

    GLuint shaderProgram = glCreateProgram(); // create program object
    glAttachShader(shaderProgram, vertexShader); // and attach both...
    glAttachShader(shaderProgram, fragmentShader); // ... shaders to it

    glLinkProgram(shaderProgram); // link the program
    glUseProgram(shaderProgram); // and select it for usage

    // now get the locations (kind of handle) of the shaders variables
    position_loc = glGetAttribLocation(shaderProgram, "position");
    phase_loc = glGetUniformLocation(shaderProgram , "phase");
    offset_loc = glGetUniformLocation(shaderProgram , "offset");
    if (position_loc < 0 || phase_loc < 0 || offset_loc < 0) {
        fputs("Unable to get uniform location\n", stderr);
        return;
    }
}

void client_render()
{
    static float phase = 0;

    glViewport(0 ,0 , Xgwa.width, Xgwa.height);
    glClearColor(0.08, 0.06, 0.07, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1f(phase_loc, phase); // write the value of phase to the shaders phase
    phase = fmodf(phase + 0.5f, 2.f * 3.141f); // and update the local variable

    GLfloat old_offset_x = offset_x;
    GLfloat old_offset_y = offset_y;
    offset_x = norm_x - p1_pos_x;
    offset_y = norm_y - p1_pos_y;
    p1_pos_x = norm_x;
    p1_pos_y = norm_y;
    offset_x += old_offset_x;
    offset_y += old_offset_y;

    glUniform4f(offset_loc, offset_x, offset_y, 0.0, 0.0);
    glVertexAttribPointer(position_loc, 3, GL_FLOAT, false, 0, vertexArray);
    glEnableVertexAttribArray(position_loc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
}

static int mousedown = 0;

void client_motionevent(int posx, int posy)
{
    if(mousedown) {
        norm_x = (2.0*posx/(float)WINDOW_WIDTH)-1.0;
        norm_y = (2.0*posy/(float)WINDOW_HEIGHT)-1.0;
    }
}

void client_mouseclickevent(int posx, int posy, int button)
{
    //fprintf(stderr,"mouse click at %d %d but: %d\n", posx, posy, button);
    mousedown = 1;
}

void client_mouseunclickevent(int posx, int posy, int button)
{
    //fprintf(stderr,"mouse unclick at %d %d but: %d\n", posx, posy, button);
    mousedown = 0;
}

void client_keypressevent(int posx, int posy, int key)
{
    //fprintf(stderr,"key press at %d %d key: %d\n", posx, posy, key);
}

void client_exposeevent()
{
    //fprintf(stderr,"window expose\n");
}

//// generic main

int main()
{
    signalinit();
    xwindowsinit();
    eglinit();

    fprintf(stderr,"Note: Press any key to quit\n");
    fprintf(stderr,"\n");
    client_glinit();
    while(sigkeeprunning & xgetevents()) { // the main loop

        client_render();

        xdisplayGLbuffer(Xwindowrect);

        if (fpsreport())
            fprintf(stderr, "fps: %f\n", fpsget());
    }

    eglcleanup();
    xwindowscleanup();
    return 0;
}
