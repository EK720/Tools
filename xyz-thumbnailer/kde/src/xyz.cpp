/*
 * This file is part of kde-xyz-thumbnailer. Copyright (c) 2020 kde-xyz-thumbnailer authors.
 * https://github.com/EasyRPG/Tools - https://easyrpg.org
 *
 * kde-xyz-thumbnailer is Free/Libre Open Source Software, released under the MIT License.
 * For the full copyright and license information, please view the COPYING
 * file that was distributed with this source code.
 */

#include "xyz.h"

#include <QString>
#include <QImage>
#include <zlib.h>

bool XyzImage::toImage(char* data, size_t size, QImage &img) {
	if (strncmp((char *) data, "XYZ1", 4) != 0) {
		return false;
	}

	unsigned short w;
	memcpy(&w, &data[4], 2);
	unsigned short h;
	memcpy(&h, &data[6], 2);

	uLongf src_size = (uLongf)(size - 8);
	Bytef* src_buffer = (Bytef*)&data[8];
	uLongf dst_size = 768 + (w * h);
	std::vector<Bytef> dst_buffer(dst_size);

	int status = uncompress(&dst_buffer.front(), &dst_size, src_buffer, src_size);

	free(data);

	if (status != Z_OK) {
		return false;
	}
	const uint8_t (*palette)[3] = (const uint8_t(*)[3]) &dst_buffer.front();

	void* pixels = malloc(w * h * 4);

	if (!pixels) {
		return false;
	}

	uint8_t* dst = (uint8_t*) pixels;
	const uint8_t* src = (const uint8_t*) &dst_buffer[768];
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8_t pix = *src++;
			const uint8_t* color = palette[pix];
			*dst++ = color[2];
			*dst++ = color[1];
			*dst++ = color[0];
			*dst++ = 255;
		}
	}
	QImage q((uchar*)pixels, w, h, QImage::Format_ARGB32);
	img = q;

	// pixels owned by QImage

	return !img.isNull();
}
