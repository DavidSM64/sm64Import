#include <vector>
#include <fstream>

#pragma once
class BMP
{
private:
	std::uint32_t width, height;
	std::uint16_t BitsPerPixel;
	std::vector<std::uint8_t> Pixels;

public:
	BMP(const char* FilePath);
	std::vector<std::uint8_t> GetPixels() const { return this->Pixels; }
	std::uint32_t GetWidth() const { return this->width; }
	std::uint32_t GetHeight() const { return this->height; }
	std::uint16_t GetBPP() const { return this->BitsPerPixel; }
	bool HasAlphaChannel() { return BitsPerPixel == 32; }
	bool error = false;
	char errorMsg[256];
};

BMP::BMP(const char* FilePath)
{	
	std::fstream hFile(FilePath, std::ios::in | std::ios::binary);
	if (!hFile.is_open()) {
		error = true;
		sprintf(errorMsg, "File not found");
		return;
	}

	hFile.seekg(0, std::ios::end);
	std::size_t Length = hFile.tellg();
	hFile.seekg(0, std::ios::beg);
	std::vector<std::uint8_t> FileInfo(Length);
	hFile.read(reinterpret_cast<char*>(FileInfo.data()), 54);

	if (FileInfo[0] != 'B' && FileInfo[1] != 'M')
	{
		hFile.close();
		error = true;
		sprintf(errorMsg, "Invalid File Format. Bitmap Required.");
		return;
	}

	if (FileInfo[28] != 8 && FileInfo[28] != 24 && FileInfo[28] != 32)
	{
		hFile.close();
		error = true;
		sprintf(errorMsg, "Invalid File Format. 8, 24, or 32 bit Image Required.");
		return;
	}

	BitsPerPixel = FileInfo[28];
	width = FileInfo[18] + (FileInfo[19] << 8);
	height = FileInfo[22] + (FileInfo[23] << 8);
	if (BitsPerPixel == 8) {
		std::uint32_t colorTable[0x100];
		hFile.read(reinterpret_cast<char*>(colorTable), 0x400);

		std::vector<std::uint8_t> PixBytes(width*height);
		hFile.read(reinterpret_cast<char*>(PixBytes.data()), width*height);
		Pixels.resize(width*height*4);
		for (int i = 0; i < PixBytes.size(); i++) {
			Pixels[i * 4 + 3] = (colorTable[PixBytes[i]] >> 24) & 0xFF;
			Pixels[i * 4 + 2] = (colorTable[PixBytes[i]] >> 16) & 0xFF;
			Pixels[i * 4 + 1] = (colorTable[PixBytes[i]] >> 8) & 0xFF;
			Pixels[i * 4 + 0] = colorTable[PixBytes[i]] & 0xFF;
		}
	}
	else {
		std::uint32_t PixelsOffset = FileInfo[10] + (FileInfo[11] << 8);
		std::uint32_t size = ((width * BitsPerPixel + 31) / 32) * 4 * height;
		Pixels.resize(size);
		hFile.seekg(PixelsOffset, std::ios::beg);
		hFile.read(reinterpret_cast<char*>(Pixels.data()), size);
	}

	hFile.close();
}