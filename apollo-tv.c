/* apollo-tv - Apollo Unified S-Band TV viewer                           */
/*=======================================================================*/
/* Copyright 2019 Philip Heron <phil@sanslogic.co.uk>                    */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <SDL2/SDL.h>
#include "sdr.h"

typedef struct {
	
	uint32_t sample_rate;
	
	int colour;
	
	int lines;
	int active_lines;
	
	int width;
	
	int hsync_width;
	int vsync_width;
	
	int active_left;
	int active_width;
	
	int fsc_left;
	int fsc_width;
	
	int frame_rate_num;
	int frame_rate_den;
	
	int frame;
	int line;
	
	int fsc;
	int fsc_hold;
	
	const int16_t *in;
	int in_len;
	
	int16_t *iline;
	int iline_len;
	
	int32_t hsync;
	int16_t *hsyncwin;
	int hsyncwin_x;
	int hsync_offset;
	
	int vsync;
	int vsync_count;
	
	int sync_level;
	int blank_level;
	int black_level;
	int white_level;
	
	uint32_t *framebuffer;
	int framebuffer_len;
	
} _usbtv_t;

/* Unified S-Band TV Decoder */
void _usbtv_free(_usbtv_t *s)
{
	free(s->framebuffer);
	free(s->hsyncwin);
	free(s->iline);
}

int _usbtv_init(_usbtv_t *s, uint32_t sample_rate, int colour)
{
	memset(s, 0, sizeof(_usbtv_t));
	
	s->sample_rate = sample_rate;
	s->colour = colour != 0;
	
	if(s->colour)
	{
		/* 525 line 30/1.001 fps interlaced field-sequential colour */
		s->lines = 525;
		s->active_lines = 480;
		s->frame_rate_num = 30000;
		s->frame_rate_den =  1001;
		
		s->hsync_width  = round(s->sample_rate * 0.00000470); /* 4.70 ±1.00µs */
		s->vsync_width  = round(s->sample_rate * 0.00002710); /* 27.10 µs */
		
		s->active_left  = round(s->sample_rate * 0.00000920); /* |-->| 9.20µs */
		s->active_width = ceil(s->sample_rate *  0.00005290); /* 52.90µs */
		
		s->fsc_left  = round(s->sample_rate * 0.00001470); /* |-->| 14.70µs */
		s->fsc_width = round(s->sample_rate * 0.00002000); /* 20.00µs */
	}
	else
	{
		/* 320 line 10 fps progressive mono */
		s->lines = 320;
		s->active_lines = 312;
		s->frame_rate_num = 10;
		s->frame_rate_den = 1;
		
		s->hsync_width  = round(s->sample_rate * 0.00002000); /* 20.00µs */
		s->vsync_width  = round(s->sample_rate * 0.00026750); /* 267.5µs */
		
		s->active_left  = round(s->sample_rate * 0.00002500); /* |-->| 25.0µs */
		s->active_width = ceil(s->sample_rate * 0.00028250); /* 282.5µs */
	}
	
	s->width = round((double) s->sample_rate / s->lines / ((double) s->frame_rate_num / s->frame_rate_den));
	
	if(s->active_width > s->width)
	{
		s->active_width = s->width;
	}
	
	s->iline_len = 0;
	s->iline = malloc(s->width * sizeof(int16_t));
	if(!s->iline)
	{
		perror("malloc");
		_usbtv_free(s);
		return(-1);
	}
	
	s->hsync = 0;
	s->hsyncwin_x = 0;
	s->hsyncwin = calloc(s->hsync_width, sizeof(int16_t));
	if(!s->hsyncwin)
	{
		perror("calloc");
		_usbtv_free(s);
		return(-1);
	}
	
	s->framebuffer_len = s->active_width * s->active_lines;
	s->framebuffer = malloc(s->framebuffer_len * sizeof(uint32_t));
	if(!s->framebuffer)
	{
		perror("malloc");
		_usbtv_free(s);
		return(-1);
	}
	
	s->frame = 1;
	s->line = 1;
	s->fsc = 0;
	s->fsc_hold = 0;
	
	fprintf(stderr, "Video: %dx%d %.2f fps (full frame %dx%d)\n",
		s->active_width, s->active_lines, (double) s->frame_rate_num / s->frame_rate_den,
		s->width, s->lines
	);
	
	fprintf(stderr, "Sample rate: %d\n", s->sample_rate);
	
	return(0);
}

int _usbtv_read(_usbtv_t *s)
{
	int aline;
	int x;
	int mx;
	int ref;
	
	while(s->iline_len < s->width)
	{
		if(s->hsync_offset < 0)
		{
			s->iline_len++;
			s->hsync_offset++;
			continue;
		}
		else if(s->iline_len > 0 && s->hsync_offset > 0)
		{
			s->iline_len--;
			s->hsync_offset--;
			continue;
		}
		
		if(s->in_len == 0) return(2);
		
		s->iline[s->iline_len] = *s->in;
		
		s->iline_len++;
		s->in++;
		s->in_len--;
	}
	
	s->iline_len = 0;
	
	/* Scan for hsync */
	mx = 0;
	ref = s->hsync;
	for(x = 0; x < s->width; x++)
	{
		/* Update the sync counter and window */
		s->hsync -= s->hsyncwin[s->hsyncwin_x];
		s->hsync += s->hsyncwin[s->hsyncwin_x] = s->iline[x];
		
		s->hsyncwin_x++;
		if(s->hsyncwin_x == s->hsync_width)
		{
			s->hsyncwin_x = 0;
		}
		
		if(s->hsync < ref)
		{
			mx = x;
			ref = s->hsync;
		}
	}
	
	ref = mx - s->hsync_width;
	if(ref < -s->width / 2) ref += s->width;
	if(ref >= s->width / 2) ref -= s->width;
	
	if(ref < 0) s->hsync_offset--;
	if(ref > 0) s->hsync_offset++;
	
	/* Update the sync level */
	ref = s->iline[1];
	for(x = 2; x < s->hsync_width - 1; x++)
	{
		ref += s->iline[x];
	}
	ref /= s->hsync_width - 2;
	
	s->sync_level = (s->sync_level * 99 + ref) / 100;
	s->blank_level = s->sync_level + (INT16_MAX * 0.3);
	
	/* Calculate the black and white levels */
	if(s->colour)
	{
		s->black_level = s->sync_level + (INT16_MAX * 0.3525);
	}
	else
	{
		s->black_level = s->sync_level + (INT16_MAX * 0.3);
	}
	
	s->white_level = s->sync_level + (INT16_MAX * 1.0);
	
	/* Scan for vsync */
	aline = 0;
	
	ref = s->iline[0];
	for(x = 1; x < s->vsync_width; x++)
	{
		ref += s->iline[x];
	}
	ref /= s->vsync_width;
	ref -= s->blank_level;
	
	s->vsync <<= 1;
	
	if(ref < -0.15 * INT16_MAX)
	{
		s->vsync |= 1;
	}
	
	if(s->colour)
	{
		x = s->width / 2;
		ref = s->iline[x];
		for(; x < s->width / 2 + s->vsync_width; x++)
		{
			ref += s->iline[x];
		}
		ref /= s->vsync_width;
		ref -= s->blank_level;
		
		s->vsync <<= 1;
		
		if(ref < -0.15 * INT16_MAX)
		{
			s->vsync |= 1;
		}
		
		s->vsync &= 0xFFFF;
		
		if(s->vsync == 252) aline = 7;
		else if(s->vsync == 126) aline = 269;
	}
	else
	{
		s->vsync &= 0x3FF;
		if(s->vsync == 510) aline = 9;
	}
	
	if(aline)
	{
		s->line = aline;
		s->vsync_count = s->lines * 10;
	}
	
	s->vsync_count += (s->vsync_count ? -1 : 0);
	
	/* Update FSC counter */
	if(s->colour)
	{
		if(s->line == 1 || s->line == 264)
		{
			s->fsc++;
			s->fsc %= 3;
			if(s->fsc == 1) s->fsc_hold = 0;
		}
		
		/* Detect the FSC flag. The hold function forces at
		 * at least one full cycle between each FSC reset. */
		
		if(!s->fsc_hold && (s->line == 18 || s->line == 281))
		{
			ref = 0;
			
			for(x = s->fsc_left; x < s->fsc_left + s->fsc_width; x++)
			{
				ref += s->iline[x];
			}
			
			ref /= s->fsc_width;
			
			if(ref > (s->white_level + s->black_level) / 2)
			{
				s->fsc = 1;
				s->fsc_hold = 1;
			}
		}
		
		aline = (s->line < 265 ? (s->line - 23) * 2 : (s->line - 286) * 2 + 1);
	}
	else
	{
		aline = s->line - 9;
	}
	
	if(aline >= 0 && aline < s->active_lines)
	{
		uint32_t c;
		int v;
		int x;
		
		for(x = 0; x < s->active_width; x++)
		{
			v = s->iline[s->active_left + x] - s->black_level;
			v = v * 255 / (s->white_level - s->black_level);
			v = (v > 0xFF ? 0xFF : (v < 0x00 ? 0x00 : v));
			
			if(s->colour)
			{
				c = s->framebuffer[aline * s->active_width + x];
				
				c &= ~(0xFF << (s->fsc * 8));
				c |= v << (s->fsc * 8);
			}
			else
			{
				c = v << 16 | v << 8 | v;
			}
			
			s->framebuffer[aline * s->active_width + x] = c;
		}
	}
	
	s->line++;
	
	if(s->line > s->lines)
	{
		s->line = 1;
		s->frame++;
		
		return(1);
	}
	
	/* In colour mode, signal to update the frame each field */
	if(s->colour && s->line == 264)
	{
		return(1);
	}
	
	return(0);
}

int _usbtv_write(_usbtv_t *s, const int16_t *buf, int samples)
{
	s->in = buf;
	s->in_len = samples;
	
	return(0);
}

static void _print_usage(void)
{
	return;
}

int main(int argc, char *argv[])
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_Event event;
	unsigned int timer;
	int c;
	int option_index;
	char *device = NULL;
	uint32_t sample_rate = 2250000;
	uint32_t deviation = 125000;
	uint32_t frequency = 855250000;
	int error_ppm = 0;
	static struct option long_options[] = {
		{ "mode",       required_argument, 0, 'm' },
		{ "device",     required_argument, 0, 'd' },
		{ "samplerate", required_argument, 0, 's' },
		{ "frequency",  required_argument, 0, 'f' },
		{ "ppm",        required_argument, 0, 'p' },
		{ "type",       required_argument, 0, 't' },
		{ "fullscreen", no_argument,       0, 'F' },
		{ 0,            0,                 0,  0  }
	};
	int done;
	int colour = 0;
	int fullscreen = 0;
	sdr_t sdr;
	_usbtv_t tv;
	int r;
	int tpf;
	double fm = 0;
	
	/* Temp buffer */
	int16_t buf[1024 * 2];
	
	opterr = 0;
	while((c = getopt_long(argc, argv, "m:d:s:f:p:t:FO", long_options, &option_index)) != -1)
	{
		switch(c)
		{
		case 'm': /* -m, --mode <name> */
			if(strcmp(optarg, "mono") == 0)
			{
				colour = 0;
			}
			else if(strcmp(optarg, "colour") == 0 ||
			        strcmp(optarg, "color") == 0)
			{
				colour = 1;
			}
			else
			{
				fprintf(stderr, "Unrecognised mode '%s'.\n", optarg);
				return(-1);
			}
			
			break;
		
		case 'd': /* -d, --device <type> */
			free(device);
			device = strdup(optarg);
			break;
		
		case 's': /* -s, --samplerate <value> */
			sample_rate = atol(optarg);
			break;
		
		case 'f': /* -f, --frequency <value> */
			frequency = atol(optarg);
			break;
		
		case 'p': /* -p, --ppm <error PPM> */
			error_ppm = atol(optarg);
			break;
		
		case 'F': /* -f, --fullscreen */
			fullscreen = 1;
			break;
		
		case '?':
			_print_usage();
			return(0);
		}
	}
	
	
	
	if(sample_rate == 0)
	{
		fprintf(stderr, "No sample rate specified.\n");
		return(-1);
	}
	
	
	
	/* Configuration is complete! Lets begin ... */
	if(device == NULL || strcmp(device, "file") == 0)
	{
		if(optind == argc)
		{
			fprintf(stderr, "No input specified.\n");
			return(-1);
		}
		
		if(sdr_open_file(&sdr, argv[optind]))
		{
			fprintf(stderr, "Error opening file '%s'.\n", argv[optind]);
			return(-1);
		}
	}
	else if(strcmp(device, "rtlsdr") == 0)
	{
		if(sdr_open_rtlsdr(&sdr, 0, sample_rate, frequency, -1, error_ppm) < 0)
		{
			fprintf(stderr, "Error opening SDR input.\n");
			return(-1);
		}
	}
	else
	{
		fprintf(stderr, "Unrecognised device '%s'.\n", device);
		return(-1);
	}
	
	if(_usbtv_init(&tv, sample_rate, colour) != 0)
	{
		fprintf(stderr, "Error initialising decoder.\n");
		return(-1);
	}
	
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		fprintf(stderr, "Error: %s\n", SDL_GetError());
		return(-1);
	}
	
	if(SDL_CreateWindowAndRenderer(tv.active_lines * 4 / 3, tv.active_lines, SDL_WINDOW_RESIZABLE, &window, &renderer) < 0)
	{
		fprintf(stderr, "Error: %s\n", SDL_GetError());
		return(-1);
	}
	
	SDL_SetWindowTitle(window, "Apollo TV Viewer");
	SDL_SetWindowFullscreen(window, (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best"); /* nearest | linear | best */
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	SDL_RenderSetLogicalSize(renderer, tv.active_lines * 4 / 3, tv.active_lines);
	
	/* Create the surface we'll be rendering into */
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tv.active_width, tv.active_lines);
	
	/* Calculate the ticks per frame (or field for the colour mode) */
	tpf = 1000 * tv.frame_rate_den / tv.frame_rate_num;
	if(tv.colour) tpf /= 2;
	
	timer = SDL_GetTicks() + tpf;
	
	/* Enter the main loop */
	done = 0;
	fullscreen = 0;
	c = optind;
	
	while(!done)
	{
		while((r = _usbtv_read(&tv)) == 2)
		{
			int i;
			
			r = sdr_read(&sdr, buf, 1024);
			if(r <= 0) break;
			
			/* Demod FM */
			for(i = 0; i < r; i++)
			{
				double d2, d = atan2(buf[i * 2], buf[i * 2 + 1]);
				
				d2 = fm - d;
				if(d2 < -M_PI) d2 += M_PI * 2;
				if(d2 >= M_PI) d2 -= M_PI * 2;
				buf[i] = round(d2 * ((sample_rate / (2.0 * M_PI)) / deviation) * INT16_MAX);
				fm = d;
			}
			
			_usbtv_write(&tv, buf, r);
		}
		
		if(r == 1)
		{
			unsigned int t;
			
			/* Limit FPS */
			t = SDL_GetTicks();
			if(t < timer)
			{
				SDL_Delay(timer - t);
				timer += tpf;
			}
			else
			{
				timer = t + tpf;
			}
			
			/* A frame has been decoded. Push and display the frame */
			SDL_UpdateTexture(texture, NULL, tv.framebuffer, tv.active_width * sizeof(uint32_t));
			SDL_RenderClear(renderer);
			SDL_RenderCopy(renderer, texture, NULL, NULL);
			SDL_RenderPresent(renderer);
		}
		else if(r < 0)
		{
			/* There's been an error. Break out */
			done = 1;
		}
		
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
			case SDL_KEYDOWN:
				
				if(event.key.keysym.sym == SDLK_ESCAPE ||
				   event.key.keysym.sym == SDLK_q)
				{
					done = 1;
				}
				else if(event.key.keysym.sym == SDLK_f)
				{
					fullscreen = !fullscreen;
					SDL_SetWindowFullscreen(window, (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
				}
				break;
			
			case SDL_QUIT:
				done = 1;
				break;
			}
		}
	}
	
	SDL_Quit();
	_usbtv_free(&tv);
	
	printf("\nDone!\n");
	
	return(0);
}

