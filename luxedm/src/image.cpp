/* SLiM - Simple Login Manager
 *  Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
 *  Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
 *  Copyright (C) 2012	Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 *  Copyright (C) 2022-23 Rob Pearce <slim@flitspace.org.uk>
 *  Copyright (C) 2026 Jerry Howell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The following code has been adapted and extended from
 *  xplanet 1.0.1, Copyright (C) 2002-04 Hari Nair <hari@alumni.caltech.edu>
 */

#include <algorithm>
#include <cctype>
#include <csetjmp>
#include <cmath>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

using namespace std;

#include "log.h"
#include "const.h"
#include "image.h"

extern "C" {
	#include <jpeglib.h>
	#include <png.h>
}

namespace {

bool image_size(const int width, const int height, const size_t channels,
		size_t &bytes)
{
	if (width <= 0 || height <= 0 || width >= MAX_DIMENSION
			|| height >= MAX_DIMENSION)
		return false;

	const size_t pixels = static_cast<size_t>(width)
			* static_cast<size_t>(height);
	if (pixels > std::numeric_limits<size_t>::max() / channels)
		return false;

	bytes = pixels * channels;
	if (bytes > MAX_IMAGE_BYTES)
		return false;
	return true;
}

unsigned char *allocate_image(const int width, const int height,
		const size_t channels)
{
	size_t bytes = 0;
	if (!image_size(width, height, channels, bytes))
		return NULL;
	return static_cast<unsigned char *>(malloc(bytes));
}

unsigned char clamp_round(const double value)
{
	if (value <= 0.0)
		return 0;
	if (value >= 255.0)
		return 255;
	return static_cast<unsigned char>(std::lround(value));
}

bool parse_hex_color(const char *text, unsigned char (&rgb)[3])
{
	if (text == NULL)
		return false;
	while (*text == '#')
		++text;
	const size_t length = strlen(text);
	if (length == 0 || length > 6)
		return false;

	char *end = NULL;
	errno = 0;
	const unsigned long value = strtoul(text, &end, 16);
	if (errno == ERANGE || end == text || *end != '\0' || value > 0xffffffUL)
		return false;
	rgb[0] = static_cast<unsigned char>((value >> 16) & 0xff);
	rgb[1] = static_cast<unsigned char>((value >> 8) & 0xff);
	rgb[2] = static_cast<unsigned char>(value & 0xff);
	return true;
}

struct JpegError {
	jpeg_error_mgr manager;
	jmp_buf jump;
	char message[JMSG_LENGTH_MAX];
};

void jpeg_error_exit(j_common_ptr context)
{
	JpegError *error = reinterpret_cast<JpegError *>(context->err);
	(*context->err->format_message)(context, error->message);
	longjmp(error->jump, 1);
}

} // namespace

Image::Image()
	: width(0), height(0), area(0), rgb_data(NULL), png_alpha(NULL)
{
}

Image::Image ( const int w, const int h, const unsigned char *rgb,
		const unsigned char *alpha)
	: width(0), height(0), area(0), rgb_data(NULL), png_alpha(NULL)
{
	if (rgb == NULL)
		return;

	unsigned char *new_rgb = allocate_image(w, h, 3);
	unsigned char *new_alpha = alpha == NULL ? NULL : allocate_image(w, h, 1);
	if (new_rgb == NULL || (alpha != NULL && new_alpha == NULL)) {
		free(new_rgb);
		free(new_alpha);
		return;
	}

	size_t rgb_bytes = 0;
	size_t alpha_bytes = 0;
	image_size(w, h, 3, rgb_bytes);
	image_size(w, h, 1, alpha_bytes);
	memcpy(new_rgb, rgb, rgb_bytes);
	if (new_alpha != NULL)
		memcpy(new_alpha, alpha, alpha_bytes);

	width = w;
	height = h;
	area = w * h;
	rgb_data = new_rgb;
	png_alpha = new_alpha;
}

Image::~Image()
{
	free(rgb_data);
	free(png_alpha);
}

bool Image::Read(const char *filename)
{
	char buf[4];
	unsigned char *ubuf = (unsigned char *) buf;
	int success = 0;
	int nr;

	const int fd = open(filename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return false;
	struct stat file_stat;
	if (fstat(fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
			file_stat.st_size < 0 ||
			static_cast<uint64_t>(file_stat.st_size) > MAX_IMAGE_BYTES) {
		close(fd);
		return false;
	}
	FILE *file = fdopen(fd, "rb");
	if (file == NULL) {
		close(fd);
		return false;
	}

	/* see what kind of file we have */

	nr = fread(buf, 1, 4, file);
	fclose(file);
	if ( nr < 4 )
		return false;	// Failed to read 4 bytes; probably empty file

	int new_width = 0;
	int new_height = 0;
	unsigned char *new_rgb = NULL;
	unsigned char *new_alpha = NULL;
	if ((ubuf[0] == 0x89) && !strncmp("PNG", buf+1, 3))
		success = readPng(filename, &new_width, &new_height, &new_rgb, &new_alpha);
	else if ((ubuf[0] == 0xff) && (ubuf[1] == 0xd8))
		success = readJpeg(filename, &new_width, &new_height, &new_rgb);
	else
	{
		fprintf(stderr, "Unknown image format\n");
		success = 0;
	}
	if (success != 1) {
		free(new_rgb);
		free(new_alpha);
		return false;
	}

	free(rgb_data);
	free(png_alpha);
	width = new_width;
	height = new_height;
	area = width * height;
	rgb_data = new_rgb;
	png_alpha = new_alpha;
	return true;
}

void Image::Reduce(const int factor)
{
	if (factor < 1 || rgb_data == NULL || width <= 0 || height <= 0)
		return;

	if (factor >= static_cast<int>(sizeof(int) * 8 - 1))
		return;
	const int scale = 1 << factor;
	const int w = width / scale;
	const int h = height / scale;
	if (w <= 0 || h <= 0)
		return;

	unsigned char *new_rgb = allocate_image(w, h, 3);
	if (new_rgb == NULL)
		return;

	unsigned char *new_alpha = NULL;
	if (png_alpha != NULL) {
		new_alpha = allocate_image(w, h, 1);
		if (new_alpha == NULL) {
			free(new_rgb);
			return;
		}
	}

	const unsigned int samples = static_cast<unsigned int>(scale * scale);
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			unsigned int rgb_sum[3] = {0, 0, 0};
			unsigned int alpha_sum = 0;
			for (int sy = 0; sy < scale; ++sy) {
				for (int sx = 0; sx < scale; ++sx) {
					const int source = (j * scale + sy) * width + i * scale + sx;
					for (int k = 0; k < 3; ++k)
						rgb_sum[k] += rgb_data[3 * source + k];
					if (png_alpha != NULL)
						alpha_sum += png_alpha[source];
				}
			}
			const int target = j * w + i;
			for (int k = 0; k < 3; ++k)
				new_rgb[3 * target + k] = static_cast<unsigned char>(rgb_sum[k] / samples);
			if (new_alpha != NULL)
				new_alpha[target] = static_cast<unsigned char>(alpha_sum / samples);
		}
	}

	free(rgb_data);
	free(png_alpha);

	rgb_data = new_rgb;
	png_alpha = new_alpha;
	width = w;
	height = h;

	area = w * h;
}

void Image::Resize(const int w, const int h)
{
	if (rgb_data == NULL || width <= 0 || height <= 0 || w <= 0 || h <= 0
			|| w >= MAX_DIMENSION || h >= MAX_DIMENSION)
		return;

	if (width==w && height==h) {
		return;
	}

	unsigned char *new_rgb = allocate_image(w, h, 3);
	if (new_rgb == NULL)
		return;
	unsigned char *new_alpha = NULL;
	if (png_alpha != NULL) {
		new_alpha = allocate_image(w, h, 1);
		if (new_alpha == NULL) {
			free(new_rgb);
			return;
		}
	}

	const double scale_x = ((double) w) / width;
	const double scale_y = ((double) h) / height;

	int ipos = 0;
	for (int j = 0; j < h; j++) {
		const double y = j / scale_y;
		for (int i = 0; i < w; i++) {
			const double x = i / scale_x;
			if (new_alpha == NULL)
				getPixel(x, y, new_rgb + 3*ipos);
			else
				getPixel(x, y, new_rgb + 3*ipos, new_alpha + ipos);
			ipos++;
		}
	}

	free(rgb_data);
	free(png_alpha);

	rgb_data = new_rgb;
	png_alpha = new_alpha;
	width = w;
	height = h;

	area = w * h;
}

/* Find the color of the desired point using bilinear interpolation. */
/* Assume the array indices refer to the center of the pixel, so each */
/* pixel has corners at (i - 0.5, j - 0.5) and (i + 0.5, j + 0.5) */
void Image::getPixel(double x, double y, unsigned char *pixel)
{
	getPixel(x, y, pixel, NULL);
}

void Image::getPixel(double x, double y, unsigned char *pixel, unsigned char *alpha)
{
	if (pixel == NULL || rgb_data == NULL || width <= 0 || height <= 0) {
		if (alpha != NULL)
			*alpha = 255;
		return;
	}
	if (!std::isfinite(x))
		x = 0.0;
	if (!std::isfinite(y))
		y = 0.0;

	if (x < -0.5)
		x = -0.5;
	if (x >= width - 0.5)
		x = width - 0.5;

	if (y < -0.5)
		y = -0.5;
	if (y >= height - 0.5)
		y = height - 0.5;

	int ix0 = (int) (floor(x));
	int ix1 = ix0 + 1;
	if (ix0 < 0)
		ix0 = 0;
	if (ix0 >= width)
		ix0 = width - 1;
	if (ix1 < 0)
		ix1 = 0;
	if (ix1 >= width)
		ix1 = width - 1;

	int iy0 = (int) (floor(y));
	int iy1 = iy0 + 1;
	if (iy0 < 0)
		iy0 = 0;
	if (iy1 >= height)
		iy1 = height - 1;

	const double t = x - floor(x);
	const double u = 1 - (y - floor(y));

	double weight[4];
	weight[1] = t * u;
	weight[0] = u - weight[1];
	weight[2] = 1 - t - u + weight[1];
	weight[3] = t - weight[1];

	unsigned char *pixels[4];
	pixels[0] = rgb_data + 3 * (iy0 * width + ix0);
	pixels[1] = rgb_data + 3 * (iy0 * width + ix1);
	pixels[2] = rgb_data + 3 * (iy1 * width + ix0);
	pixels[3] = rgb_data + 3 * (iy1 * width + ix1);

	double value[3] = {0.0, 0.0, 0.0};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 3; j++)
			value[j] += weight[i] * pixels[i][j];
	}
	for (int j = 0; j < 3; ++j)
		pixel[j] = clamp_round(value[j]);

	if (alpha != NULL && png_alpha == NULL) {
		*alpha = 255;
	} else if (alpha != NULL) {
		unsigned char pixels[4];
		pixels[0] = png_alpha[iy0 * width + ix0];
		pixels[1] = png_alpha[iy0 * width + ix1];
		pixels[2] = png_alpha[iy1 * width + ix0];
		pixels[3] = png_alpha[iy1 * width + ix1];

		double alpha_value = 0.0;
		for (int i = 0; i < 4; i++)
			alpha_value += weight[i] * pixels[i];
		*alpha = clamp_round(alpha_value);
	}
}


/**
 * Merge the image with a background, taking care of the image Alpha
 * transparency. (background alpha is ignored).
 *
 * The image is merged with the section of background at position (x, y).
 * The background must fully contain the image.
 * If the image does not have any transparency (no alpha data) then this
 * is a no-operation.
 * @note use of double to calculate the new value of a U8
 */
void Image::Merge ( const Image* background, const int x, const int y )
{
	if (background == NULL || rgb_data == NULL || background->getRGBData() == NULL
	  || background->Width() <= 0 || background->Height() <= 0
	  || width <= 0 || height <= 0
	  || x < 0 || y < 0
	  || x > background->Width() - width
	  || y > background->Height() - height )
		return;

	if (png_alpha != NULL)
	{
		unsigned char *new_rgb = allocate_image(width, height, 3);
		if (new_rgb == NULL)
			return;
		const unsigned char *bg_rgb = background->getRGBData();
		double tmp;
		int opos = 0;
		for (int j = 0; j < height; j++)
		{
			int ipos = (y+j) * background->Width() + x;
			for (int i = 0; i < width; i++)
			{
				for (int k = 0; k < 3; k++)
				{
					tmp = rgb_data[3*opos + k]*png_alpha[opos]/255.0
							+ bg_rgb[3*ipos + k]*(1-png_alpha[opos]/255.0);
					new_rgb[3*opos + k] = clamp_round(tmp);
				}
				opos++;
				ipos++;
			}
		}
		free(rgb_data);
		free(png_alpha);
		rgb_data = new_rgb;
		png_alpha = NULL;
	}
}


/* Merge the image with a background, taking care of the
 * image Alpha transparency. (background alpha is ignored).
 * The images is merged on position (x, y) on the
 * background, the background must contain the image.
 */
#define IMG_POS_RGB(p, x) (3 * p + x)
void Image::Merge_non_crop(Image* background, const int x, const int y)
{
	if (background == NULL || rgb_data == NULL || background->getRGBData() == NULL)
		return;
	int bg_w = background->Width();
	int bg_h = background->Height();

	if (width <= 0 || height <= 0 || bg_w <= 0 || bg_h <= 0 ||
			x < 0 || y < 0 || x > bg_w - width || y > bg_h - height)
		return;

	double tmp;
	unsigned char *new_rgb = allocate_image(bg_w, bg_h, 3);
	if (new_rgb == NULL)
		return;
	const unsigned char *bg_rgb = background->getRGBData();
	int pnl_pos = 0;
	int bg_pos = 0;
	int pnl_w_end = x + width;
	int pnl_h_end = y + height;

	size_t bytes = 0;
	image_size(bg_w, bg_h, 3, bytes);
	memcpy(new_rgb, bg_rgb, bytes);

	for (int j = 0; j < bg_h; j++) {
		for (int i = 0; i < bg_w; i++) {
			if (j >= y && i >= x && j < pnl_h_end && i < pnl_w_end ) {
				for (int k = 0; k < 3; k++) {
					if (png_alpha != NULL)
						tmp = rgb_data[IMG_POS_RGB(pnl_pos, k)]
							* png_alpha[pnl_pos]/255.0
							+ bg_rgb[IMG_POS_RGB(bg_pos, k)]
							* (1 - png_alpha[pnl_pos]/255.0);
					else
						tmp = rgb_data[IMG_POS_RGB(pnl_pos, k)];

					new_rgb[IMG_POS_RGB(bg_pos, k)] = clamp_round(tmp);
				}
				pnl_pos++;
			}
			bg_pos++;
		}
	}

	width = bg_w;
	height = bg_h;
	area = width * height;

	free(rgb_data);
	free(png_alpha);
	rgb_data = new_rgb;
	png_alpha = NULL;
}


/* Tile the image growing its size to the minimum entire
 * multiple of w * h.
 * The new dimensions should be > of the current ones.
 * Note that this flattens image (alpha removed)
 */
void Image::Tile(const int w, const int h)
{
	if (rgb_data == NULL || width <= 0 || height <= 0 || w < width || h < height
			|| w <= 0 || h <= 0 || w >= MAX_DIMENSION || h >= MAX_DIMENSION)
		return;

	unsigned char *new_rgb = allocate_image(w, h, 3);
	if (new_rgb == NULL)
		return;

	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			const int source = (j % height) * width + (i % width);
			const int target = j * w + i;
			memcpy(new_rgb + 3 * target, rgb_data + 3 * source, 3);
		}
	}

	free(rgb_data);
	free(png_alpha);
	rgb_data = new_rgb;
	png_alpha = NULL;
	width = w;
	height = h;
	area = width * height;
}


/* Crop the image
 */
void Image::Crop(const int x, const int y, const int w, const int h)
{
	if (rgb_data == NULL || x < 0 || y < 0 || w <= 0 || h <= 0
			|| x > width - w || y > height - h) {
		return;
	}

	int x2 = x + w;
	int y2 = y + h;
	unsigned char *new_rgb = allocate_image(w, h, 3);
	if (new_rgb == NULL)
		return;
	unsigned char *new_alpha = NULL;
	const bool had_alpha = png_alpha != NULL;
	if (had_alpha) {
		new_alpha = allocate_image(w, h, 1);
		if (new_alpha == NULL) {
			free(new_rgb);
			return;
		}
	}

	int ipos = 0;
	int opos = 0;

	for (int j = 0; j < height; j++) {
		for (int i = 0; i < width; i++) {
			if (j>=y && i>=x && j<y2 && i<x2) {
				for (int k = 0; k < 3; k++) {
					new_rgb[3*ipos + k] = static_cast<unsigned char> (rgb_data[3*opos + k]);
				}
				if (had_alpha)
					new_alpha[ipos] = static_cast<unsigned char> (png_alpha[opos]);
				ipos++;
			}
			opos++;
		}
	}

	free(rgb_data);
	free(png_alpha);
	rgb_data = new_rgb;
	png_alpha = new_alpha;
	width = w;
	height = h;
	area = w * h;
}


/* Center the image in a rectangle of given width and height.
 * Fills the remaining space (if any) with the hex color
 */
void Image::Center(const int w, const int h, const char *hex)
{
	if (rgb_data == NULL || w <= 0 || h <= 0 || w >= MAX_DIMENSION
			|| h >= MAX_DIMENSION || hex == NULL)
		return;
	unsigned char rgb[3];
	if (!parse_hex_color(hex, rgb))
		return;
	const unsigned char r = rgb[0];
	const unsigned char g = rgb[1];
	const unsigned char b = rgb[2];

	unsigned char *new_rgb = allocate_image(w, h, 3);
	if (new_rgb == NULL)
		return;

	const int source_x = width > w ? (width - w) / 2 : 0;
	const int source_y = height > h ? (height - h) / 2 : 0;
	const int destination_x = width < w ? (w - width) / 2 : 0;
	const int destination_y = height < h ? (h - height) / 2 : 0;
	const int copy_width = std::min(width, w);
	const int copy_height = std::min(height, h);

	const int new_area = w * h;
	for (int i = 0; i < new_area; i++) {
		new_rgb[3*i] = r;
		new_rgb[3*i+1] = g;
		new_rgb[3*i+2] = b;
	}

	for (int j = 0; j < copy_height; ++j) {
		for (int i = 0; i < copy_width; ++i) {
			const int source = (source_y + j) * width + source_x + i;
			const int target = (destination_y + j) * w + destination_x + i;
			for (int k = 0; k < 3; ++k) {
				if (png_alpha != NULL) {
					const double opacity = png_alpha[source] / 255.0;
					new_rgb[3 * target + k] = clamp_round(
							rgb_data[3 * source + k] * opacity
							+ new_rgb[3 * target + k] * (1.0 - opacity));
				} else {
					new_rgb[3 * target + k] = rgb_data[3 * source + k];
				}
			}
		}
	}

	free(rgb_data);
	free(png_alpha);
	rgb_data = new_rgb;
	png_alpha = NULL;
	width = w;
	height = h;
	area = new_area;
}


/* Fill the image with the given color and adjust its dimensions
 * to passed values.
 */
void Image::Plain(const int w, const int h, const char *hex)
{
	if (w <= 0 || h <= 0 || w >= MAX_DIMENSION || h >= MAX_DIMENSION
			|| hex == NULL)
		return;
	unsigned char rgb[3];
	if (!parse_hex_color(hex, rgb))
		return;
	const unsigned char r = rgb[0];
	const unsigned char g = rgb[1];
	const unsigned char b = rgb[2];

	unsigned char *new_rgb = allocate_image(w, h, 3);
	if (new_rgb == NULL)
		return;

	area = w * h;
	for (int i = 0; i < area; i++) {
		new_rgb[3*i] = r;
		new_rgb[3*i+1] = g;
		new_rgb[3*i+2] = b;
	}

	free(rgb_data);
	free(png_alpha);
	rgb_data = new_rgb;
	png_alpha = NULL;
	width = w;
	height = h;
}


void Image::computeShift(unsigned long mask,
					unsigned char &left_shift,
					unsigned char &right_shift)
{
	left_shift = 0;
	right_shift = 8;
	if (mask != 0) {
		while ((mask & 0x01) == 0) {
			left_shift++;
			mask >>= 1;
		}
		while ((mask & 0x01) == 1) {
			right_shift--;
			mask >>= 1;
		}
	}
	if (right_shift > 128) {
		left_shift += 255 - right_shift + 1;
		right_shift = 0;
	}
}


Pixmap Image::createPixmap(Display* dpy, int scr, Window win)
{
	if (dpy == NULL || scr < 0 || scr >= XScreenCount(dpy) || win == None ||
			rgb_data == NULL || width <= 0 || height <= 0)
		return None;

	int i, j;   /* loop variables */

	const int depth = DefaultDepth(dpy, scr);
	Visual *visual = DefaultVisual(dpy, scr);
	Colormap colormap = DefaultColormap(dpy, scr);

	Pixmap tmp = XCreatePixmap(dpy, win, width, height, depth);
	if (tmp == None)
		return None;

	size_t bytes_per_pixel = 0;
	switch (depth) {
	case 32:
	case 24:
		bytes_per_pixel = 4;
		break;
	case 16:
	case 15:
		bytes_per_pixel = 2;
		break;
	case 8:
		bytes_per_pixel = 1;
		break;
	default:
		if (depth > 24)
			bytes_per_pixel = 4;
		break;
	}

	if (bytes_per_pixel == 0) {
		logStream << APPNAME << ": unsupported X visual depth " << depth << endl;
		XFreePixmap(dpy, tmp);
		return None;
	}
	const size_t pixels = static_cast<size_t>(width) * height;
	char *pixmap_data = static_cast<char *>(calloc(pixels, bytes_per_pixel));
	if (pixmap_data == NULL) {
		XFreePixmap(dpy, tmp);
		return None;
	}

	XImage *ximage = XCreateImage(dpy, visual, depth, ZPixmap, 0,
								  pixmap_data, width, height,
								  8, 0);
	if (ximage == NULL) {
		free(pixmap_data);
		XFreePixmap(dpy, tmp);
		return None;
	}

	int entries;
	XVisualInfo v_template;
	v_template.visualid = XVisualIDFromVisual(visual);
	XVisualInfo *visual_info = XGetVisualInfo(dpy, VisualIDMask,
								   &v_template, &entries);
	if (visual_info == NULL || entries < 1) {
		XDestroyImage(ximage);
		XFreePixmap(dpy, tmp);
		return None;
	}

	unsigned long ipos = 0;
	switch (visual_info->c_class) {
	case PseudoColor: {
			XColor xc;
			xc.flags = DoRed | DoGreen | DoBlue;

			int num_colors = 256;
			XColor *colors = new XColor[num_colors];
			for (i = 0; i < num_colors; i++)
				colors[i].pixel = (unsigned long) i;
			XQueryColors(dpy, colormap, colors, num_colors);

			int *closest_color = new int[num_colors];

			for (i = 0; i < num_colors; i++) {
				xc.red = (i & 0xe0) << 8;		   /* highest 3 bits */
				xc.green = (i & 0x1c) << 11;		/* middle 3 bits */
				xc.blue = (i & 0x03) << 14;		 /* lowest 2 bits */

				/* find the closest color in the colormap */
				double distance, distance_squared, min_distance = 0;
				for (int ii = 0; ii < num_colors; ii++) {
					distance = colors[ii].red - xc.red;
					distance_squared = distance * distance;
					distance = colors[ii].green - xc.green;
					distance_squared += distance * distance;
					distance = colors[ii].blue - xc.blue;
					distance_squared += distance * distance;

					if ((ii == 0) || (distance_squared <= min_distance)) {
						min_distance = distance_squared;
						closest_color[i] = ii;
					}
				}
			}

			for (j = 0; j < height; j++) {
				for (i = 0; i < width; i++) {
					xc.red = (unsigned short) (rgb_data[ipos++] & 0xe0);
					xc.green = (unsigned short) (rgb_data[ipos++] & 0xe0);
					xc.blue = (unsigned short) (rgb_data[ipos++] & 0xc0);

					xc.pixel = xc.red | (xc.green >> 3) | (xc.blue >> 6);
					XPutPixel(ximage, i, j,
							  colors[closest_color[xc.pixel]].pixel);
				}
			}
			delete [] colors;
			delete [] closest_color;
		}
		break;
	case TrueColor: {
			unsigned char red_left_shift;
			unsigned char red_right_shift;
			unsigned char green_left_shift;
			unsigned char green_right_shift;
			unsigned char blue_left_shift;
			unsigned char blue_right_shift;

			computeShift(visual_info->red_mask, red_left_shift,
						 red_right_shift);
			computeShift(visual_info->green_mask, green_left_shift,
						 green_right_shift);
			computeShift(visual_info->blue_mask, blue_left_shift,
						 blue_right_shift);

			unsigned long pixel;
			unsigned long red, green, blue;
			for (j = 0; j < height; j++) {
				for (i = 0; i < width; i++) {
					red = (unsigned long)
						  rgb_data[ipos++] >> red_right_shift;
					green = (unsigned long)
							rgb_data[ipos++] >> green_right_shift;
					blue = (unsigned long)
						   rgb_data[ipos++] >> blue_right_shift;

					pixel = (((red << red_left_shift) & visual_info->red_mask)
							 | ((green << green_left_shift)
								& visual_info->green_mask)
							 | ((blue << blue_left_shift)
								& visual_info->blue_mask));

					XPutPixel(ximage, i, j, pixel);
				}
			}
		}
		break;
	default: {
			logStream << APPNAME << ": could not load image" << endl;
			XFree(visual_info);
			XDestroyImage(ximage);
			XFreePixmap(dpy, tmp);
			return None;
		}
	}

	GC gc = XCreateGC(dpy, win, 0, NULL);
	if (gc == NULL) {
		XFree(visual_info);
		XDestroyImage(ximage);
		XFreePixmap(dpy, tmp);
		return None;
	}
	XPutImage(dpy, tmp, gc, ximage, 0, 0, 0, 0, width, height);

	XFreeGC(dpy, gc);

	XFree(visual_info);

	XDestroyImage(ximage);

	return(tmp);
}

int Image::readJpeg(const char *filename, int *width, int *height,
				unsigned char **rgb)
{
	if (filename == NULL || width == NULL || height == NULL || rgb == NULL)
		return 0;

	*width = 0;
	*height = 0;
	*rgb = NULL;
	const int infd = open(filename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (infd < 0) {
		logStream << APPNAME << ": cannot open image: " << filename << endl;
		return 0;
	}
	struct stat image_stat;
	if (fstat(infd, &image_stat) != 0 || !S_ISREG(image_stat.st_mode) ||
			image_stat.st_size < 0 ||
			static_cast<uint64_t>(image_stat.st_size) > MAX_IMAGE_BYTES) {
		close(infd);
		return 0;
	}
	FILE *infile = fdopen(infd, "rb");
	if (infile == NULL) {
		close(infd);
		return 0;
	}

	jpeg_decompress_struct cinfo;
	memset(&cinfo, 0, sizeof(cinfo));
	JpegError error;
	cinfo.err = jpeg_std_error(&error.manager);
	error.manager.error_exit = jpeg_error_exit;
	volatile bool decoder_created = false;
	if (setjmp(error.jump)) {
		logStream << APPNAME << ": invalid JPEG " << filename << ": "
				<< error.message << endl;
		if (decoder_created)
			jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		free(*rgb);
		*rgb = NULL;
		return 0;
	}

	jpeg_create_decompress(&cinfo);
	decoder_created = true;
	jpeg_stdio_src(&cinfo, infile);
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return 0;
	}
	cinfo.out_color_space = JCS_RGB;
	if (jpeg_start_decompress(&cinfo) != TRUE) {
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return 0;
	}

	if (cinfo.output_width == 0 || cinfo.output_height == 0
			|| cinfo.output_width >= MAX_DIMENSION
			|| cinfo.output_height >= MAX_DIMENSION
			|| cinfo.output_components != 3) {
		logStream << APPNAME << "Unreasonable dimension found in file: "
				  << filename << endl;
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return 0;
	}

	const int decoded_width = static_cast<int>(cinfo.output_width);
	const int decoded_height = static_cast<int>(cinfo.output_height);
	unsigned char *decoded = allocate_image(decoded_width, decoded_height, 3);
	if (decoded == NULL) {
		logStream << APPNAME << ": Can't allocate memory for JPEG file."
				  << endl;
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return 0;
	}

	while (cinfo.output_scanline < cinfo.output_height) {
		unsigned char *row = decoded
				+ static_cast<size_t>(cinfo.output_scanline) * decoded_width * 3;
		if (jpeg_read_scanlines(&cinfo, &row, 1) != 1) {
			free(decoded);
			jpeg_destroy_decompress(&cinfo);
			fclose(infile);
			return 0;
		}
	}

	if (jpeg_finish_decompress(&cinfo) != TRUE) {
		free(decoded);
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return 0;
	}
	jpeg_destroy_decompress(&cinfo);
	fclose(infile);
	*width = decoded_width;
	*height = decoded_height;
	*rgb = decoded;
	return 1;
}

int Image::readPng(const char *filename, int *width, int *height,
			   unsigned char **rgb, unsigned char **alpha)
{
	if (filename == NULL || width == NULL || height == NULL || rgb == NULL
			|| alpha == NULL)
		return 0;

	*width = 0;
	*height = 0;
	*rgb = NULL;
	*alpha = NULL;
	png_image image;
	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_file(&image, filename)) {
		logStream << APPNAME << ": invalid PNG " << filename << ": "
				<< image.message << endl;
		return 0;
	}

	if (image.width == 0 || image.height == 0 || image.width >= MAX_DIMENSION
			|| image.height >= MAX_DIMENSION) {
		logStream << APPNAME << ": unreasonable PNG dimensions: " << filename
				<< endl;
		png_image_free(&image);
		return 0;
	}

	const int decoded_width = static_cast<int>(image.width);
	const int decoded_height = static_cast<int>(image.height);
	image.format = PNG_FORMAT_RGBA;
	size_t rgba_bytes = PNG_IMAGE_SIZE(image);
	if (rgba_bytes == 0 || rgba_bytes > MAX_IMAGE_BYTES) {
		logStream << APPNAME << ": unreasonable PNG dimensions: " << filename
				  << endl;
		png_image_free(&image);
		return 0;
	}
	unsigned char *rgba = static_cast<unsigned char *>(malloc(rgba_bytes));
	unsigned char *decoded_rgb = allocate_image(decoded_width, decoded_height, 3);
	unsigned char *decoded_alpha = allocate_image(decoded_width, decoded_height, 1);
	if (rgba == NULL || decoded_rgb == NULL || decoded_alpha == NULL) {
		logStream << APPNAME << ": can't allocate memory for PNG file." << endl;
		free(rgba);
		free(decoded_rgb);
		free(decoded_alpha);
		png_image_free(&image);
		return 0;
	}

	if (!png_image_finish_read(&image, NULL, rgba, 0, NULL)) {
		logStream << APPNAME << ": invalid PNG " << filename << ": "
				<< image.message << endl;
		free(rgba);
		free(decoded_rgb);
		free(decoded_alpha);
		png_image_free(&image);
		return 0;
	}

	const size_t pixels = static_cast<size_t>(decoded_width) * decoded_height;
	for (size_t i = 0; i < pixels; ++i) {
		decoded_rgb[3 * i] = rgba[4 * i];
		decoded_rgb[3 * i + 1] = rgba[4 * i + 1];
		decoded_rgb[3 * i + 2] = rgba[4 * i + 2];
		decoded_alpha[i] = rgba[4 * i + 3];
	}

	free(rgba);
	png_image_free(&image);
	*width = decoded_width;
	*height = decoded_height;
	*rgb = decoded_rgb;
	*alpha = decoded_alpha;
	return 1;
}
