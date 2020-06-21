/*
droid VNC server - a vnc server for android
Copyright (C) 2011 Jose Pereira <onaips@gmail.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#define OUT_T CONCAT3E(uint,OUT,_t)
#define FUNCTION CONCAT2E(update_screen_,OUT)

void FUNCTION(void)
{
	int r;
	OUT_T* b=0;
	struct fb_var_screeninfo scrinfo; //we'll need this to detect double FB on framebuffer

	if (display_rotate_180){
		r=rotation;
		rotation+=180;
	}

	if (method==FRAMEBUFFER) {
		scrinfo = FB_getscrinfo();
		b = (OUT_T*) readBufferFB();
	}
	else if (method==ADB)
		b = (OUT_T*) readBufferADB();
	else if (method==GRALLOC)
		b = (OUT_T*) readBufferGralloc();
	else if (method==FLINGER)
		b = (OUT_T*) readBufferFlinger();

	int max_x=-1,max_y=-1, min_x=99999, min_y=99999;
	idle=0;

	if (!idle) {
		memcpy(vncbuf,b,screenformat.width*screenformat.height*screenformat.bitsPerPixel/CHAR_BIT);

		min_x--;
		min_y--;
		max_x++;
		max_y++;

		//L("Changed x(%d-%d) y(%d-%d)\n",min_x,max_x,min_y,max_y);

		rfbMarkRectAsModified(vncscr, 0, 0, screenformat.width, screenformat.height);
	}

	if (display_rotate_180)
		rotation=r;
}



