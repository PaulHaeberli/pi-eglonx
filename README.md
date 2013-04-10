Raspberry Pi EGL on X: Update 10 Apr 2013
----------------------------------------

This is an update of eglonx for Raspberry pi. This fixes crashes on start up and
a pixel swapping error on the software conversion to 565 for drawing on to the X 
window. Also added more complete handling of X window mouse and expose events.

Fixed startup crash by replacing this:

	Ximage = XGetImage(Xdsp, Xwin, 0, 0, Xgwa.width, Xgwa.height, AllPlanes, ZPixmap);

with

	char *buf = (char *)malloc(Xgwa.width*Xgwa.height*2);
	Ximage = XCreateImage(Xdsp,
                DefaultVisual(Xdsp, DefaultScreen(Xdsp)),
                DefaultDepth(Xdsp, DefaultScreen(Xdsp)),
                ZPixmap, 0, buf, Xgwa.width, Xgwa.height, 16, 0);

And added this on exit:

	bcm_host_deinit();


Paul



Raspberry Pi EGL on X
---------------------

Demo of Raspbery Pi OpenGL ES rendering to X window posted to raspberrypi.org
forums by @teh_orph at http://www.raspberrypi.org/phpBB3/viewtopic.php?f=63&t=6488

Converted to c from c++, added Makefile. Tested on Raspbian.

to run type

	cd pi-eglonx
	make
	./eglonx

Post by @teh_orph

	Hello guys,

	Quick proof-of-concept: hardware accelerated 3D in an X window. 
	You do not need any fancy X server to make this work. The code provided 
	below only works in a 16-bit 5/6/5 screen though.

	Brief summary:
	- I took this code: http://wiki.maemo.org/SimpleGL_example
	- replaced the eglCreateWindowSurface with eglCreatePixmapSurface
	- replaced the swap buffers with a glReadPixels and an XPutImage

	Since everything's done in X, all window hiding stuff works fine. You can 
	run multiple EGL programs too. (In the video I run the same program twice - 
	no sharing is being done between them if you're wondering!)

	http://www.youtube.com/watch?v=l0i55EndDZQ

	CAVEATS:
	- eglCreatePixmapSurface seems to be ill-documented for the Broadcom 3D EGL 
	API. Figuring out what to put as the pixmap argument appears to be guesswork!
	- X is currently invisible in 24/32-bit mode, and I can't get the pixmap to run 
	in 16-bit mode. So I *in software* glReadPixels the image in 32-bit mode, and 
	then convert to 5/6/5 by hand...this is of course slow. Once it's figured out 
	how to make either a 16-bit pixmap or get 32-bit X working, no conversion will 
	be required.

	NB: glReadPixels is powered by the hardware's DMA functionality once the GPU 
	is flushed. The X server it is running on in the video has DMA-enabled blitting, 
	so - assuming the 32->16 conversion is omitted - the whole process would be 
	powered by DMA.

