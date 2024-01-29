#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>
#include "GL/glew.h"
#include "../extra/picopng.h"
#include "image.h"
#include "utils.h"
#include "camera.h"
#include "mesh.h"
#include "particlesystem.h"

Image::Image() {
	width = 0; height = 0;
	pixels = NULL;
}

Image::Image(unsigned int width, unsigned int height)
{
	this->width = width;
	this->height = height;
	pixels = new Color[width*height];
	memset(pixels, 0, width * height * sizeof(Color));
}

// Copy constructor
Image::Image(const Image& c)
{
	pixels = NULL;
	width = c.width;
	height = c.height;
	bytes_per_pixel = c.bytes_per_pixel;
	if(c.pixels)
	{
		pixels = new Color[width*height];
		memcpy(pixels, c.pixels, width*height*bytes_per_pixel);
	}
}

// Assign operator
Image& Image::operator = (const Image& c)
{
	if(pixels) delete pixels;
	pixels = NULL;

	width = c.width;
	height = c.height;
	bytes_per_pixel = c.bytes_per_pixel;

	if(c.pixels)
	{
		pixels = new Color[width*height*bytes_per_pixel];
		memcpy(pixels, c.pixels, width*height*bytes_per_pixel);
	}
	return *this;
}

Image::~Image()
{
	if(pixels) 
		delete pixels;
}

void Image::Render()
{
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glDrawPixels(width, height, bytes_per_pixel == 3 ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

// Change image size (the old one will remain in the top-left corner)
void Image::Resize(unsigned int width, unsigned int height)
{
	Color* new_pixels = new Color[width*height];
	unsigned int min_width = this->width > width ? width : this->width;
	unsigned int min_height = this->height > height ? height : this->height;

	for(unsigned int x = 0; x < min_width; ++x)
		for(unsigned int y = 0; y < min_height; ++y)
			new_pixels[ y * width + x ] = GetPixel(x,y);

	delete pixels;
	this->width = width;
	this->height = height;
	pixels = new_pixels;
}

// Change image size and scale the content
void Image::Scale(unsigned int width, unsigned int height)
{
	Color* new_pixels = new Color[width*height];

	for(unsigned int x = 0; x < width; ++x)
		for(unsigned int y = 0; y < height; ++y)
			new_pixels[ y * width + x ] = GetPixel((unsigned int)(this->width * (x / (float)width)), (unsigned int)(this->height * (y / (float)height)) );

	delete pixels;
	this->width = width;
	this->height = height;
	pixels = new_pixels;
}

Image Image::GetArea(unsigned int start_x, unsigned int start_y, unsigned int width, unsigned int height)
{
	Image result(width, height);
	for(unsigned int x = 0; x < width; ++x)
		for(unsigned int y = 0; y < height; ++x)
		{
			if( (x + start_x) < this->width && (y + start_y) < this->height) 
				result.SetPixel( x, y, GetPixel(x + start_x,y + start_y) );
		}
	return result;
}

void Image::FlipY()
{
	int row_size = bytes_per_pixel * width;
	Uint8* temp_row = new Uint8[row_size];
#pragma omp simd
	for (int y = 0; y < height * 0.5; y += 1)
	{
		Uint8* pos = (Uint8*)pixels + y * row_size;
		memcpy(temp_row, pos, row_size);
		Uint8* pos2 = (Uint8*)pixels + (height - y - 1) * row_size;
		memcpy(pos, pos2, row_size);
		memcpy(pos2, temp_row, row_size);
	}
	delete[] temp_row;
}

bool Image::LoadPNG(const char* filename, bool flip_y)
{
	std::string sfullPath = absResPath(filename);
	std::ifstream file(sfullPath, std::ios::in | std::ios::binary | std::ios::ate);

	// Get filesize
	std::streamsize size = 0;
	if (file.seekg(0, std::ios::end).good()) size = file.tellg();
	if (file.seekg(0, std::ios::beg).good()) size -= file.tellg();

	if (!size)
		return false;

	std::vector<unsigned char> buffer;

	// Read contents of the file into the vector
	if (size > 0)
	{
		buffer.resize((size_t)size);
		file.read((char*)(&buffer[0]), size);
	}
	else
		buffer.clear();

	std::vector<unsigned char> out_image;

	if (decodePNG(out_image, width, height, buffer.empty() ? 0 : &buffer[0], (unsigned long)buffer.size(), true) != 0)
		return false;

	size_t bufferSize = out_image.size();
	unsigned int originalBytesPerPixel = (unsigned int)bufferSize / (width * height);
	
	// Force 3 channels
	bytes_per_pixel = 3;

	if (originalBytesPerPixel == 3) {
		pixels = new Color[bufferSize];
		memcpy(pixels, &out_image[0], bufferSize);
	}
	else if (originalBytesPerPixel == 4) {

		unsigned int newBufferSize = width * height * bytes_per_pixel;
		pixels = new Color[newBufferSize];

		unsigned int k = 0;
		for (unsigned int i = 0; i < bufferSize; i += originalBytesPerPixel) {
			pixels[k] = Color(out_image[i], out_image[i + 1], out_image[i + 2]);
			k++;
		}
	}

	// Flip pixels in Y
	if (flip_y)
		FlipY();

	return true;
}

// Loads an image from a TGA file
bool Image::LoadTGA(const char* filename, bool flip_y)
{
	unsigned char TGAheader[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	unsigned char TGAcompare[12];
	unsigned char header[6];
	unsigned int imageSize;
	unsigned int bytesPerPixel;

    std::string sfullPath = absResPath( filename );

	FILE * file = fopen( sfullPath.c_str(), "rb");
   	if ( file == NULL || fread(TGAcompare, 1, sizeof(TGAcompare), file) != sizeof(TGAcompare) ||
		memcmp(TGAheader, TGAcompare, sizeof(TGAheader)) != 0 ||
		fread(header, 1, sizeof(header), file) != sizeof(header))
	{
		std::cerr << "File not found: " << sfullPath.c_str() << std::endl;
		if (file == NULL)
			return NULL;
		else
		{
			fclose(file);
			return NULL;
		}
	}

	TGAInfo* tgainfo = new TGAInfo;
    
	tgainfo->width = header[1] * 256 + header[0];
	tgainfo->height = header[3] * 256 + header[2];
    
	if (tgainfo->width <= 0 || tgainfo->height <= 0 || (header[4] != 24 && header[4] != 32))
	{
		fclose(file);
		delete tgainfo;
		return NULL;
	}
    
	tgainfo->bpp = header[4];
	bytesPerPixel = tgainfo->bpp / 8;
	imageSize = tgainfo->width * tgainfo->height * bytesPerPixel;
    
	tgainfo->data = new unsigned char[imageSize];
    
	if (tgainfo->data == NULL || fread(tgainfo->data, 1, imageSize, file) != imageSize)
	{
		if (tgainfo->data != NULL)
			delete tgainfo->data;
            
		fclose(file);
		delete tgainfo;
		return false;
	}

	fclose(file);

	// Save info in image
	if(pixels)
		delete pixels;

	width = tgainfo->width;
	height = tgainfo->height;
	pixels = new Color[width*height];

	// Convert to float all pixels
	for (unsigned int y = 0; y < height; ++y) {
		for (unsigned int x = 0; x < width; ++x) {
			unsigned int pos = y * width * bytesPerPixel + x * bytesPerPixel;
			// Make sure we don't access out of memory
			if( (pos < imageSize) && (pos + 1 < imageSize) && (pos + 2 < imageSize))
				SetPixel(x, height - y - 1, Color(tgainfo->data[pos + 2], tgainfo->data[pos + 1], tgainfo->data[pos]));
		}
	}

	// Flip pixels in Y
	if (flip_y)
		FlipY();

	delete tgainfo->data;
	delete tgainfo;

	return true;
}

// Saves the image to a TGA file
bool Image::SaveTGA(const char* filename)
{
	unsigned char TGAheader[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	std::string fullPath = absResPath(filename);
	FILE *file = fopen(fullPath.c_str(), "wb");
	if ( file == NULL )
	{
		perror("Failed to open file: ");
		return false;
	}

	unsigned short header_short[3];
	header_short[0] = width;
	header_short[1] = height;
	unsigned char* header = (unsigned char*)header_short;
	header[4] = 24;
	header[5] = 0;

	fwrite(TGAheader, 1, sizeof(TGAheader), file);
	fwrite(header, 1, 6, file);

	// Convert pixels to unsigned char
	unsigned char* bytes = new unsigned char[width*height*3];
	for(unsigned int y = 0; y < height; ++y)
		for(unsigned int x = 0; x < width; ++x)
		{
			Color c = pixels[y*width+x];
			unsigned int pos = (y*width+x)*3;
			bytes[pos+2] = c.r;
			bytes[pos+1] = c.g;
			bytes[pos] = c.b;
		}

	fwrite(bytes, 1, width*height*3, file);
	fclose(file);

	return true;
}

void Image::DrawLineDDA(int x0, int y0, int x1, int y1, const Color& c)
{
    // Calculate the differences between endpoints
    int dx = x1 - x0;
    int dy = y1 - y0;
    
    // Compute d as the largest leg of the triangle
    int d = std::max(std::abs(dx), std::abs(dy));
    
    // Compute the direction step vector v(dx/d, dy/d)
    float xIncrement = (float)dx / d;
    float yIncrement = (float)dy / d;
    
    
    float x = x0;
    float y = y0;
    
    // Drawing each pixel to conform the line
    for (int i = 0; i <= d; ++i) {
        int x_floor = (int)std::floor(x);
        int y_floor = (int)std::floor(y);
        
        // Check that the pixels are inside the image
        if (x_floor >= 0 && x_floor < width && y_floor >= 0 && y_floor < height) {
            pixels[y_floor * width + x_floor] = c;
        }
        
        // Move to the next pixel
        x += xIncrement;
        y += yIncrement;
    }
}
    

void Image::DrawRect(int x, int y, int w, int h, const Color& borderColor, int borderWidth, bool isFilled, const Color& fillColor)
{
    // Check if the rectangle is out of bounds
    if (x < 0 || y < 0 || x + w > width || y + h > height) {
        //Checking that the rectangle is well drawn
        return;
    }

    // Draw the filled rectangle
    if (isFilled) {
        for (int i = 0; i < w; ++i) {
            for (int j = 0; j < h; ++j) {
                SetPixel(x + i, y + j, fillColor);
            }
        }
    }
    
    // Draw the border
    for (int i = 0; i < borderWidth; ++i) {
        // Draw top border
        for (int j = x - i; j < x + w + i; ++j) {
            SetPixelSafe(j, y - i, borderColor);
        }

        // Draw bottom border
        for (int j = x - i; j < x + w + i; ++j) {
            SetPixelSafe(j, y + h + i - 1, borderColor);
        }

        // Draw left border
        for (int j = y - i; j < y + h + i; ++j) {
            SetPixelSafe(x - i, j, borderColor);
        }

        // Draw right border
        for (int j = y - i; j < y + h + i; ++j) {
            SetPixelSafe(x + w + i - 1, j, borderColor);
        }
    }
}

void Image::DrawCircle(int x, int y, int r, const Color& borderColor, int borderWidth, bool isFilled, const Color& fillColor) {
    // Check if the circle is out of bounds
    if (x - r < 0 || x + r >= width || y - r < 0 || y + r >= height) {
        //Checking that the circle is well drawn
        return;
    }

    // Draw the filled circle
    if (isFilled) {
        for (int i = -r; i <= r; ++i) {
            for (int j = -r; j <= r; ++j) {
                if (i * i + j * j <= r * r) {
                    SetPixelSafe(x + i, y + j, fillColor);
                }
            }
        }
    }
    // Draw the border
    for (int i = 0; i < borderWidth; ++i) {
        int currentRadius = r + i;  // r + i so it goes to the outside, not the inside
        for (int angle = 0; angle <= 360; ++angle) {
            int xPos = (int)(currentRadius * std::cos(angle * 3.14159265 / 180.0));
            int yPos = (int)(currentRadius * std::sin(angle * 3.14159265 / 180.0));

            SetPixelSafe(x + xPos, y + yPos, borderColor);
        }
    }
}

void Image::ScanLineDDA(int x0, int y0, int x1, int y1, std::vector<Cell>& table) {
    // Calculate the differences between endpoints
    int dx = x1 - x0;
    int dy = y1 - y0;
    
    // Compute d as the largest leg of the triangle
    int d = std::max(std::abs(dx), std::abs(dy));
    
    // Compute the direction step vector v(dx/d, dy/d)
    float vx = (float)(dx) / d;
    float vy = (float)(dy) / d;

    float x = x0;
    float y = y0;

    for (int i = 0; i <= d; ++i) {
        int currentY = (int)(floor(y));
        for (int j = 0; j < table.size(); ++j) {
            if (table[j].y == currentY) {
                table[j].minx = std::min(table[j].minx, (int)(floor(x)));
                table[j].maxx = std::max(table[j].maxx, (int)(floor(x)));
            }
        }
        x += vx;
        y += vy;
    }
}


void Image::DrawTriangle(const Vector2& p0, const Vector2& p1, const Vector2& p2, const Color& borderColor, bool isFilled, const Color& fillColor) {
    float topPoint = std::max(p0.y, p1.y);
    topPoint = std::max(topPoint, p2.y);
    float minPoint = std::min(p0.y, p1.y);
    minPoint = std::min(minPoint, p2.y);
    int h = (int)(topPoint - minPoint);

    std::vector<Cell> table;
    table.resize(h);

    for (int i = 0; i < h; ++i) {
        table[i].y = (int)(minPoint) + i;
    }

    ScanLineDDA(p0.x, p0.y, p1.x, p1.y, table);
    ScanLineDDA(p1.x, p1.y, p2.x, p2.y, table);
    ScanLineDDA(p2.x, p2.y, p0.x, p0.y, table);

    for (int i = 0; i < h; ++i) {
        SetPixelSafe(table[i].minx, minPoint + i, borderColor);
        SetPixelSafe(table[i].maxx, minPoint + i, borderColor);
        if (isFilled) {
            for (int x = table[i].minx + 1; x < table[i].maxx; ++x) {
                SetPixelSafe(x, minPoint + i, fillColor);
            }
        }
    }
    for (int i = table[0].minx; i < table[0].maxx; ++i) {
        SetPixelSafe(i, minPoint, borderColor);
    }
}


void Image::DrawImage(const Image& image, int x, int y, bool top) {
    // Check if the image is out of bounds
    if (x < 0 || y < 0 || x + image.width > width || y + image.height > height) {
        printf("Image out of bounds");
        return;
    }

    // Determine the starting row for drawing based on the 'top' parameter
    int startY = top ? y : y - image.height + 1;

    // Draw the image to the framebuffer
    for (int i = 0; i < image.width; ++i) {
        for (int j = 0; j < image.height; ++j) {
            // Get the pixel color from the image
            Color pixel = image.GetPixel(i, j);

            // Calculate the destination coordinates in the framebuffer
            int destX = x + i;
            int destY = startY + j;

            // Set the pixel in the framebuffer
            SetPixelSafe(destX, destY, pixel);
        }
    }
}

#ifndef IGNORE_LAMBDAS

// You can apply and algorithm for two images and store the result in the first one
// ForEachPixel( img, img2, [](Color a, Color b) { return a + b; } );
template <typename F>
void ForEachPixel(Image& img, const Image& img2, F f) {
	for(unsigned int pos = 0; pos < img.width * img.height; ++pos)
		img.pixels[pos] = f( img.pixels[pos], img2.pixels[pos] );
}

#endif

FloatImage::FloatImage(unsigned int width, unsigned int height)
{
	this->width = width;
	this->height = height;
	pixels = new float[width * height];
	memset(pixels, 0, width * height * sizeof(float));
}

// Copy constructor
FloatImage::FloatImage(const FloatImage& c) {
	pixels = NULL;

	width = c.width;
	height = c.height;
	if (c.pixels)
	{
		pixels = new float[width * height];
		memcpy(pixels, c.pixels, width * height * sizeof(float));
	}
}

// Assign operator
FloatImage& FloatImage::operator = (const FloatImage& c)
{
	if (pixels) delete pixels;
	pixels = NULL;

	width = c.width;
	height = c.height;
	if (c.pixels)
	{
		pixels = new float[width * height * sizeof(float)];
		memcpy(pixels, c.pixels, width * height * sizeof(float));
	}
	return *this;
}

FloatImage::~FloatImage()
{
	if (pixels)
		delete pixels;
}

// Change image size (the old one will remain in the top-left corner)
void FloatImage::Resize(unsigned int width, unsigned int height)
{
	float* new_pixels = new float[width * height];
	unsigned int min_width = this->width > width ? width : this->width;
	unsigned int min_height = this->height > height ? height : this->height;

	for (unsigned int x = 0; x < min_width; ++x)
		for (unsigned int y = 0; y < min_height; ++y)
			new_pixels[y * width + x] = GetPixel(x, y);

	delete pixels;
	this->width = width;
	this->height = height;
	pixels = new_pixels;
}

Button::Button(Image& c, unsigned int x_pos, unsigned int y_pos) {
    this->x = x_pos;
    this->y = y_pos;
    this->image = &c;
}
              
              
bool Button::IsMouseInside(Vector2 mouse_position) {
    // Check if the mouse position is in the button position
    if ((mouse_position.x >= this->x) && (mouse_position.x <= (this->x + 32)) &&
        (mouse_position.y >= this->y) && (mouse_position.y <= (this->y + 32))) {
        return true;
    } else {
        return false;
    }
}

void Image::DrawBlack(int x, int y, int width, int height) {
    // Draw the black rectangle to the framebuffer
    for (int i = 0; i < width; ++i) {
        for (int j = 0; j < height; ++j) {
            // Calculate the destination coordinates in the framebuffer
            int destX = x + i;
            int destY = y + j;

            // Set the pixel in the framebuffer to black
            SetPixel(destX, destY, {0,0,0});
        }
    }
}

void Image::DrawFree(int x0, int y0, int x1, int y1, const Color& color) {
    // Calculate differences and absolute differences between coordinates
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;

    //Differentiate
    int err = dx - dy;

    while (x0 != x1 || y0 != y1) {
        // Plot the pixel
        SetPixelSafe(x0, y0, color);

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}




