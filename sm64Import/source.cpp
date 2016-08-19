/*
sm64Import v0.1.1 by Davideesk

Todo list in the future:
Fix reduce vertices level 2 bug.
Support CI textures.
Improve water boxes.
*/

#include "rapidxml.hpp"
#include "BMP.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>
#include <cstdio>
using namespace std;
using namespace rapidxml;

// clang and gcc >= 4.4.3
#if (defined(__GNUC__ ) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)))
#define BYTE_SWAP_16(x) __builtin_bswap16(x)
#define BYTE_SWAP_32(x) __builtin_bswap32(x)
#elif (defined(__clang__))
#define BYTE_SWAP_16(x) __builtin_bswap16(x)
#define BYTE_SWAP_32(x) __builtin_bswap32(x)
#elif defined(_MSC_VER)
#include <cstdlib>
#define BYTE_SWAP_16(x) _byteswap_ushort(x)
#define BYTE_SWAP_32(x) _byteswap_ulong(x)
#else
#error Not clang, gcc, or MSC_VER
#endif

// from: http://stackoverflow.com/questions/6089231
istream& safeGetline(istream& is, string& t)
{
	t.clear();

	// The characters in the stream are read one-by-one using a std::streambuf.
	// That is faster than reading them one-by-one using the std::istream.
	// Code that uses streambuf this way must be guarded by a sentry object.
	// The sentry object performs various tasks,
	// such as thread synchronization and updating the stream state.

	istream::sentry se(is, true);
	streambuf* sb = is.rdbuf();

	for (;;) {
		int c = sb->sbumpc();
		switch (c) {
		case '\n':
			return is;
		case '\r':
			if (sb->sgetc() == '\n')
				sb->sbumpc();
			return is;
		case EOF:
			// Also handle the case when the last line has no line ending
			if (t.empty())
				is.setstate(ios::eofbit);
			return is;
		default:
			t += (char)c;
		}
	}
}

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define	G_TX_DXT_FRAC	11
#define TXL2WORDS(txls, b_txl)	MAX(1, ((txls)*(b_txl)/8))
#define CALC_DXT(width, b_txl)	\
		(((1 << G_TX_DXT_FRAC) + TXL2WORDS(width, b_txl) - 1) / \
					TXL2WORDS(width, b_txl))
#define TXL2WORDS_4b(txls)	MAX(1, ((txls)/16))
#define CALC_DXT_4b(width)	\
		(((1 << G_TX_DXT_FRAC) + TXL2WORDS_4b(width) - 1) / \
					TXL2WORDS_4b(width))

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

enum MaterialType
{
	NOT_DEFINED,
	TEXTURE_SOLID,
	TEXTURE_ALPHA,
	TEXTURE_TRANSPARENT,
	COLOR_SOLID,
	COLOR_TRANSPARENT
}; 

enum TextureType
{
	TEX_RGBA16,
	TEX_RGBA32,
	TEX_IA4,
	TEX_IA8,
	TEX_IA16,
	TEX_I4,
	TEX_I8
};

/* Names taken straight from skelux's importer */
enum FogType
{
	FOG_SUBTLE_1,
	FOG_SUBTLE_2,
	FOG_MODERATE_1,
	FOG_MODERATE_2,
	FOG_MODERATE_3,
	FOG_MODERATE_4,
	FOG_INTENSE,
	FOG_VERY_INTENSE,
	FOG_HARDCORE
};

enum PtrType
{
	PTR_START,
	PTR_SOLID,
	PTR_ALPHA,
	PTR_TRANS,
	PTR_GEO,
	PTR_COL
};

typedef struct {
	s16 x, y, z;
} vertex;

typedef struct {
	u8 a, b, c, d;
} normal; // Can also be RGBA color in some cases

typedef struct {
	s16 u, v; 
} texCord;

typedef struct {
	string name;
	bool hasTexture;
	bool hasTextureAlpha;
	bool hasTransparency;
	bool isTextureCopy;
	bool enableTextureColor;
	bool enableAlphaMask; // For I4/I8 textures.
	bool cameFromBMP; // For I4/I8 textures.
	u32 color;
	u8 opacity;
	u8 opacityOrg;
	u32 offset;
	u32 texColOffset;
	float texColDark;
	u32 size;
	u32 texWidth;
	u32 texHeight;
	MaterialType type;
	TextureType texType;
	u16 collision;
	bool enableGeoMode;
	u32 geoMode;
	u32 texture;
} material;

typedef struct {
	string data;
	material * mat;
} face;

typedef struct {
	u8 data[16];
} finalVertex;

typedef struct {
	s16 numTri;
	s16 size;
	s8 indexList[0x800];
	vector<finalVertex> fv;
} fvGroup;

typedef struct {
	u8 data[8];
} f3d;

typedef struct {
	u8 len;
	u8 data[0x20];
} geoCmd;

typedef struct {
	u8 len;
	u8 data[0x100];
} genData;

typedef struct {
	int position;
	int cLen;
	material * mat;
	int numGroups;
	int vertStart;
	vector<fvGroup> fvg;
} materialPosition;

typedef struct {
	u32 id;
	u32 offset;
	u32 width;
	u32 height;
	vector<u8> data;
} textureEntry;

/*
Types:
0 = water
1 = toxic mist
2 = mist
*/
typedef struct {
	u16 index;
	u16 type;
	u16 scale;
	s16 minx;
	s16 minz;
	s16 maxx;
	s16 maxz;
	s16 y;
	u32* pointer;
} waterBox;

typedef struct {
	string name;
	u32 start_ptr;
	u32 solid_ptr;
	u32 alpha_ptr;
	u32 trans_ptr;
	u32 geo_ptr;
	u32 col_ptr;
} objPtrs;

typedef struct {
	u32 address;
	u32 curPos;
	u32 limit;
	u8 * data;
} import;

typedef struct {
	u32 data;
	u32 address;
} putData;

typedef struct {
	u16 id;
	string message;
} error;

typedef struct {
	u32 address;
	vector<u8> data;
} tweakPatch;

vector<import> importData;
import* curImp = NULL;
u32 setNextLimit = 0;
error err;

vector<vertex> verts;
vector<normal> norms;
vector<texCord> uvs;
vector<material> materials;
vector<materialPosition> matPos;
vector<face> faces;
vector<finalVertex> combinedVerts;
vector<textureEntry> textureBank;
vector<objPtrs> ptrs;
vector<putData> putImports;
vector<tweakPatch> tweaks;

material* currentMaterial;
int currentFace = 0;
int lastPos = 0;
int lastOffset = 0;
int lastFVertPos = 0;
bool createSolidDL = false;
bool createAlphaDL = false;
bool createTransDL = false;
bool definedSegPtr = false;
MaterialType lastTextureType = NOT_DEFINED;
string geoModeData = "", colorTexData = "", texTypeData = "";
string * currentPreName = NULL;

/* Tool Variables */
string romPath;
string collisionString; 
string curObjPath = "";
u32 romPos = 0xFFFFFFFF;
u8 curSeg = 0;
u32 startSegOffset = 0;
u32 curSegOffset = 0;
bool verbose = false;
bool flipTexturesVertically = false;
bool centerVerts = false;
/*
Reduces the number of duplicate verticies.
Level 0 = No reduction.
Level 1 = Reduce only in the same 0x04 group.
Level 2 = Reduce and push up. (A little buggy, will fully fix in next version)
*/
u8 reduceVertLevel = 1;
float modelScale = 1.0f;
s16 xOff = 0;
s16 yOff = 0;
s16 zOff = 0;
bool enableFog = false;
FogType fogType = FOG_VERY_INTENSE;
u8 fogRed = 0xFF, fogGreen = 0xFF, fogBlue = 0xFF;
u8 defaultColor[16] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x7F,0x7F,0xFF,0x7F,0x7F,0x7F,0xFF };
bool enableDeathFloor = false;
s16 dfx1 = 0x4000, dfz1 = 0x4000, dfx2 = 0xBFFF, dfz2 = 0xBFFF, dfy = -8000;
vector<waterBox> waterBoxes;

void importOBJCollision(const char*, const char*, bool);

bool checkLittleEndian()
{
	unsigned int x = 1;
	char *c = (char*)&x;
	return (int)*c;
}

// Debug thing
void printHexBytes(u8 * arr, int size) {
	for (int i = 0; i < size; i++) printf("%02X ", arr[i]);
	printf("\n");
}

// Debug thing
void printHexShorts(u16 * arr, int size) {
	for (int i = 0; i < size; i++) printf("%04X ", arr[i]);
	printf("\n");
}

u8 bytesPerType(TextureType type) {
	switch (type) {
		case TEX_RGBA32:
			return 4;
		case TEX_I4:
		case TEX_IA4:
			return 0; // Special case
		case TEX_IA8:
		case TEX_I8:
			return 1;
		case TEX_IA16:
		case TEX_RGBA16:
		default:
			return 2;
	}
}

u8 getTexelIncrement(TextureType type) {
	switch (type) {
		case TEX_I4:
		case TEX_IA4:
			return 3;
		case TEX_IA8:
		case TEX_I8:
			return 1;
		case TEX_RGBA32:
		case TEX_IA16:
		case TEX_RGBA16:
		default:
			return 0;
	}
}

u8 getTexelShift(TextureType type) {
	switch (type) {
		case TEX_I4:
		case TEX_IA4:
			return 2;
		case TEX_IA8:
		case TEX_I8:
			return 1;
		case TEX_RGBA32:
		case TEX_IA16:
		case TEX_RGBA16:
		default:
			return 0;
	}
}

bool fileExists(const std::string& name) {
	if (FILE *file = fopen(name.c_str(), "r")) {
		fclose(file);
		return true;
	}
	return false;
}

string tolowercase(string str) {
	for (char& c : str) c = tolower(c);
	return str;
}
string tolowercase(char* str) {
	return tolowercase(string(str));
}

string trim(const string &s)
{
	string::const_iterator it = s.begin();
	while (it != s.end() && isspace(*it))
		it++;

	string::const_reverse_iterator rit = s.rbegin();
	while (rit.base() != it && isspace(*rit))
		rit++;

	return string(it, rit.base());
}

vector<string> splitString(string str, string split) {
	vector<string> result;
	char * pch = strtok((char*)str.c_str(), (char*)split.c_str());
	while (pch != NULL) {
		result.push_back(pch);
		pch = strtok(NULL, (char*)split.c_str());
	}
	return result;
}

void centerVertices() {
	if (centerVerts) {
		int avgX = 0, avgY = 0, avgZ = 0;
		for (vertex& v : verts) {
			avgX += v.x;
			avgY += v.y;
			avgZ += v.z;
		}
		avgX /= (int)verts.size();
		avgY /= (int)verts.size();
		avgZ /= (int)verts.size();
		for (vertex& v : verts) {
			v.x -= avgX;
			v.y -= avgY;
			v.z -= avgZ;
		}
	}
}

void addObjPtr(char* name) {
	for (objPtrs& op : ptrs) 
		if (op.name.compare(name) == 0) 
			return;
	objPtrs op;
	op.name = name;
	op.start_ptr = 0;
	op.solid_ptr = 0;
	op.alpha_ptr = 0;
	op.trans_ptr = 0;
	op.geo_ptr = 0;
	op.col_ptr = 0;
	ptrs.push_back(op);
}

void addPtr(char* name, PtrType type, u32 data) {
	if (curImp != NULL) {
		for (objPtrs& op : ptrs) {
			if (op.name.compare(name) == 0) {
				switch (type) {
					case PTR_START:
						op.start_ptr = data;
						break;
					case PTR_SOLID:
						op.solid_ptr = data;
						break;
					case PTR_ALPHA:
						op.alpha_ptr = data;
						break;
					case PTR_TRANS:
						op.trans_ptr = data;
						break;
					case PTR_GEO:
						op.geo_ptr = data;
						break;
					case PTR_COL:
						op.col_ptr = data;
						break;
				}
			}
		}
	}
}

void checkGeoModeInfo(material& m) {
	m.geoMode = 0;
	vector<string> gma = splitString(geoModeData,",");
	for (string gme : gma) {
		vector<string> gmd = splitString(gme, ":");
		if (m.name.compare(gmd[0]) == 0) {
			if (gmd[1].find("0x") == 0) 
				m.geoMode = stoll(gmd[1].substr(2), 0, 16);
			else if (gmd[1].find("$") == 0)
				m.geoMode = stoll(gmd[1].substr(1), 0, 16);
			else
				m.geoMode = stoll(gmd[1]);
			m.enableGeoMode = true;
			if (verbose) printf("Set geomtry mode for '%s', value = 0x%X\n", m.name.c_str(), m.geoMode);
			return;
		}
	}
}

void checkTextureTypeInfo(material& m) {
	vector<string> gma = splitString(texTypeData, ",");
	for (string gme : gma) {
		vector<string> gmd = splitString(gme, ":");
		//printf("Comparing %s vs %s\n", m.name.c_str(), gmd[0].c_str());
		if (m.name.compare(gmd[0]) == 0) {
			gmd[1] = tolowercase(gmd[1]);
			//printf("Set Texture type for '%s', value = %s\n", m.name.c_str(), gmd[1].c_str());
			if (gmd[1].compare("rgba16") == 0) 
				m.texType = TEX_RGBA16;
			else if (gmd[1].compare("rgba32") == 0)
				m.texType = TEX_RGBA32;
			else if (gmd[1].compare("ia4") == 0) 
				m.texType = TEX_IA4;
			else if (gmd[1].compare("ia8") == 0)
				m.texType = TEX_IA8;
			else if (gmd[1].compare("ia16") == 0)
				m.texType = TEX_IA16;
			else if (gmd[1].compare("i4") == 0) {
				m.texType = TEX_I4;
				if (gmd.size() > 2) {
					if (gmd[2].compare("a") == 0) {
						m.enableAlphaMask = true;
					}
				}
			}
			else if (gmd[1].compare("i8") == 0) {
				m.texType = TEX_I8;
				if (gmd.size() > 2) {
					if (gmd[2].compare("a") == 0) {
						m.enableAlphaMask = true;
					}
				}
			}
			return;
		}
	}
	m.texType = TEX_RGBA16;
}

void checkColorTexInfo(material& m) {
	m.color = 0;
	m.texColOffset = 0;
	m.texColDark = 0.8f;
	vector<string> gma = splitString(colorTexData, ",");
	for (string gme : gma) {
		vector<string> gmd = splitString(gme, ":");
		if (m.name.compare(gmd[0].c_str()) == 0) {
			if (gmd[1].find("0x") == 0)
				m.color = stoll(gmd[1].substr(2).c_str(), 0, 16);
			else if (gmd[1].find("$") == 0) {
				m.color = stoll(gmd[1].substr(1).c_str(), 0, 16);
			}
			else
				m.color = stoll(gmd[1].c_str());
			m.enableTextureColor = true;
			if (verbose) printf("Set texture color for '%s', value = 0x%X\n", m.name.c_str(), m.color);
			if (gmd.size() > 2) 
				m.texColDark = stod(gmd[2].c_str());
			return;
		}
	}
}

bool checkForTextureCopy(material * mat, vector<u8>& cur) {
	//printf("Checking copies for material '%s'\n", mat->name.c_str());
	if (textureBank.size() == 0) return false;
	for (textureEntry& entry : textureBank) {
		bool pass = false;
		if (entry.data.size() != cur.size()) continue;
		for (int i = 0; i < entry.data.size(); ++i) {
			if (entry.data.size() != cur.size()) break;
			if (entry.data[i] != cur[i]) { 
				pass = false;
				break;
			}
			pass = true;
		}
		if (pass) {
			mat->offset = entry.offset;
			mat->texWidth = entry.width;
			mat->texHeight = entry.height;
			mat->size = 0;
			mat->texture = entry.id;
			mat->isTextureCopy = true;
			return true;
		}
	}
	return false;
}

int decodeImage(vector<u8>& data, u32& width, u32& height, string filename) {
	if (filename.rfind('.') != string::npos) {
		if (verbose)printf("decoding image: '%s'\n", filename.c_str());
		string arg = "nconvert.exe -quiet -overwrite -o \"sm64ImportTemp.bmp\" -out bmp -32bits ";
		arg.append(string(curObjPath).append(filename));
		system(arg.c_str()); // call nconvert.exe
		BMP img = BMP("sm64ImportTemp.bmp");
		if (img.error) {
			err.id = 504;
			err.message = "Image error: ";
			err.message.append(img.errorMsg);
		}
		data = img.GetPixels();
		width = img.GetWidth();
		height = img.GetHeight();

		remove("sm64ImportTemp.bmp"); // delete file
	} else {
		err.id = 500;
		err.message = "Could not find extension for image file: ";
		err.message.append(filename.c_str());
	}

	return 0;
}

int processRGBA16Image(const char* filename, material * mat) {
	vector<u8> data;
	u32 width, height;
	
	decodeImage(data, width, height, filename);

	if (verbose) printf("RGBA16 Texture '%s': w=%d, h=%d, size = 0x%X\n",filename,width,height,data.size());
	mat->texWidth = width;
	mat->texHeight = height;
	vector<u8> tex = vector<u8>(data.size() / 2);

	mat->type = TEXTURE_SOLID;

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			u8 r = (data[(y*width + x) * 4 + 0] / 8) & 31;
			u8 g = (data[(y*width + x) * 4 + 1] / 8) & 31;
			u8 b = (data[(y*width + x) * 4 + 2] / 8) & 31;
			u8 a = (data[(y*width + x) * 4 + 3] > 0) ? 1 : 0; // 1 bit alpha
			u8 alpha = data[(y*width + x) * 4 + 3]; // 8 bit alpha

			if (a == 0) {
				mat->hasTextureAlpha = true;
				createAlphaDL = true;
				mat->type = TEXTURE_ALPHA;
			} else if (alpha < 0xFF || mat->opacity < 0xFF) {
				if (mat->type != TEXTURE_ALPHA) {
					if (mat->opacity == 0xFF) mat->opacity *= alpha;
					mat->type = TEXTURE_TRANSPARENT;
					mat->hasTransparency = true;
					createTransDL = true;
				}
			}

			bool shouldFlip = flipTexturesVertically;
			if (mat->cameFromBMP) shouldFlip = !shouldFlip;
			int pos = (y*width + x) * 2;
			if (shouldFlip)
				pos = ((height - 1 - y)*width + x) * 2;

			u16 texel = (r << 11) | (g << 6) | (b << 1) | a;
			tex[pos] = (texel >> 8) & 0xFF;
			tex[pos + 1] = texel & 0xFF;
		}
	}

	if (!checkForTextureCopy(mat, tex)) {
		textureEntry entry;
		entry.offset = lastOffset;
		entry.data = tex;
		entry.width = width;
		entry.height = height;
		entry.id = textureBank.size();
		textureBank.push_back(entry);
		mat->texture = textureBank.size() - 1;
	}

	mat->hasTexture = true;
	return data.size() / 2;
}


int processRGBA32Image(const char* filename, material * mat) {
	vector<u8> data;
	u32 width, height;

	decodeImage(data, width, height, filename);

	mat->texWidth = width;
	mat->texHeight = height;
	vector<u8> tex = vector<u8>(data.size());
	if (verbose) printf("RGBA32 Texture '%s': w=%d, h=%d, size = 0x%X\n",filename,width,height, data.size());

	mat->type = TEXTURE_SOLID;

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			u8 r = data[(y*width + x) * 4 + 0];
			u8 g = data[(y*width + x) * 4 + 1];
			u8 b = data[(y*width + x) * 4 + 2];
			u8 a = data[(y*width + x) * 4 + 3]; // 8 bit alpha

			if (a < 0xFF || mat->opacity < 0xFF) {
				if (mat->opacity == 0xFF) mat->opacity *= a;
				mat->type = TEXTURE_TRANSPARENT;
				mat->hasTransparency = true;
				createTransDL = true;
			}

			bool shouldFlip = flipTexturesVertically;
			if (mat->cameFromBMP) shouldFlip = !shouldFlip;
			int pos = (y*width + x) * 4;
			if (shouldFlip)
				pos = ((height - 1 - y)*width + x) * 4;

			tex[pos] = r;
			tex[pos + 1] = g;
			tex[pos + 2] = b;
			tex[pos + 3] = a;
		}
	}

	if (!checkForTextureCopy(mat, tex)) {
		textureEntry entry;
		entry.offset = lastOffset;
		entry.data = tex;
		entry.width = width;
		entry.height = height;
		entry.id = textureBank.size();
		textureBank.push_back(entry);
		mat->texture = textureBank.size() - 1;
	}

	mat->hasTexture = true;
	return tex.size();
}

int processIntensityImage(const char* filename, material * mat, u8 bits) {
	vector<u8> data;
	u32 width, height;

	decodeImage(data, width, height, filename);

	mat->texWidth = width;
	mat->texHeight = height;
	vector<u8> tex = vector<u8>((int)(data.size() / 4.0 * (bits/8.0)));
	if (verbose) printf("I%d Texture '%s': w=%d, h=%d, size = 0x%X\n", bits, filename, width, height, tex.size());

	mat->type = TEXTURE_SOLID;

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			u8 r = data[(y*width + x) * 4 + 0];
			u8 g = data[(y*width + x) * 4 + 1];
			u8 b = data[(y*width + x) * 4 + 2];
			u8 a = data[(y*width + x) * 4 + 3]; // 8 bit alpha

			if (a < 0xFF || mat->opacity < 0xFF || mat->enableAlphaMask) {
				if (mat->opacity == 0xFF) mat->opacity *= a;
				mat->type = TEXTURE_TRANSPARENT;
				mat->hasTransparency = true;
				createTransDL = true;
			}

			u8 intensity = ((r + g + b) / 3);
			if (bits == 4)
				intensity /= 16;

			bool shouldFlip = flipTexturesVertically;
			if (mat->cameFromBMP) shouldFlip = !shouldFlip;
			int pos = (y*width + x);
			if (shouldFlip)
				pos = ((height - 1 - y)*width + x);

			if (bits == 4) { // I4
				int npos = pos / 2;
				if(pos % 2 == 0)
					tex[npos] = (intensity & 0xF) << 4;
				else
					tex[npos] |= (intensity & 0xF);
			}
			else { // I8
				tex[pos] = intensity;
			}

		}
	}
	
	if (!checkForTextureCopy(mat, tex)) {
		textureEntry entry;
		entry.offset = lastOffset;
		entry.data = tex;
		entry.width = width;
		entry.height = height;
		entry.id = textureBank.size();
		textureBank.push_back(entry);
		mat->texture = textureBank.size() - 1;
	}
	
	mat->hasTexture = true;
	return tex.size();
}

int processIntensityAlphaImage(const char* filename, material * mat, u8 bits) {
	vector<u8> data;
	u32 width, height;

	decodeImage(data, width, height, filename);

	mat->texWidth = width;
	mat->texHeight = height;
	vector<u8> tex = vector<u8>((int)(data.size() / 4.0 * (bits / 8.0)));
	if (verbose) printf("IA%d Texture '%s': w=%d, h=%d, size = 0x%X\n", bits, filename, width, height, tex.size());

	mat->type = TEXTURE_SOLID;

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			u8 r = data[(y*width + x) * 4 + 0];
			u8 g = data[(y*width + x) * 4 + 1];
			u8 b = data[(y*width + x) * 4 + 2];
			u8 a = data[(y*width + x) * 4 + 3]; // 8 bit alpha
			u8 alpha = a;

			u8 intensity = ((r + g + b) / 3);
			if (bits == 4) {
				intensity /= 32;
				alpha = a < 0xFF ? 0 : 1;
			}
			else if (bits == 8) {
				intensity /= 16;
				alpha /= 16;
			}

			if ((alpha > 0 && alpha < 0xFF) || mat->opacity < 0xFF) {
					if (mat->opacity == 0xFF) mat->opacity *= a;
					mat->type = TEXTURE_TRANSPARENT;
					mat->hasTransparency = true;
			} else if (alpha == 0) {
				if (mat->type != TEXTURE_TRANSPARENT) {
					mat->type = TEXTURE_ALPHA;
				}
			}

			bool shouldFlip = flipTexturesVertically;
			if (mat->cameFromBMP) shouldFlip = !shouldFlip;
			int pos = (y*width + x);
			if (shouldFlip)
				pos = ((height - 1 - y)*width + x);

			if (bits == 4) { // IA4
				int npos = pos / 2;
				if (pos % 2 == 0) {
					tex[npos] = (((intensity << 1) | alpha) & 0xF) << 4;
				}
				else {
					tex[npos] |= ((intensity << 1) | alpha) & 0xF;
				}
			}
			else if (bits == 8) { // IA8
				tex[pos] = (intensity << 4) | alpha;
			}
			else { //IA16
				pos *= 2;
				tex[pos] = intensity & 0xFF;
				tex[pos + 1] = alpha & 0xFF;
			}
		}
	}

	if (mat->type == TEXTURE_ALPHA) {
		createAlphaDL = true;
		mat->hasTextureAlpha = true;
	}
	if (!checkForTextureCopy(mat, tex)) {
		textureEntry entry;
		entry.offset = lastOffset;
		entry.data = tex;
		entry.width = width;
		entry.height = height;
		entry.id = textureBank.size();
		textureBank.push_back(entry);
		mat->texture = textureBank.size() - 1;
	}

	mat->hasTexture = true;
	return tex.size();
}

void processMaterialColor(string str, material * mat) {
	vector<string> splitColor = splitString(str, " ");
	int r = stod(splitColor[0]) * 255;
	int g = stod(splitColor[1]) * 255;
	int b = stod(splitColor[2]) * 255;
	mat->color = r << 24 | g << 16 | b << 8 | 0xFF;
}

void processMaterialColorAlpha(float alpha, material * mat) {
	mat->color &= 0xFFFFFF00;
	mat->color |= (u8)(0xFF * alpha);
	mat->type = COLOR_TRANSPARENT;
	if (alpha < 1.0f) {
		mat->hasTransparency = true;
		createTransDL = true;
	}
}

void processMaterialLib(string str) {
	lastOffset = curSegOffset;
	string line;
	ifstream infile;
	infile.open(str);
	int size = 0;
	while (safeGetline(infile, line)) {
		if (line.find("newmtl ") == 0) {
			material m;
			m.type = COLOR_SOLID;
			m.color = 0;
			m.hasTexture = false;
			m.hasTextureAlpha = false; 
			m.hasTransparency = false;
			m.isTextureCopy = false;
			m.name = line.substr(7);
			m.collision = 0;
			m.opacity = 0xFF;
			m.opacityOrg = 0xFF;
			m.enableGeoMode = false;
			m.enableTextureColor = false;
			m.enableAlphaMask = false;
			m.cameFromBMP = false;
			checkGeoModeInfo(m);
			checkColorTexInfo(m);
			materials.push_back(m);
			if (materials.size() > 1) {
				if (size == 0) size = 0x10;
				if (!materials.at(materials.size() - 2).isTextureCopy) {
					(&materials.at(materials.size() - 2))->offset = lastOffset;
					(&materials.at(materials.size() - 2))->size = size;
					lastOffset += size;
				}
				if (materials.at(materials.size() - 2).enableTextureColor) {
					lastOffset += 0x10;
				}
			}
			size = 0x10;
		} else if (line.find("Kd ") == 0) {
			if(!materials.at(materials.size() - 1).enableTextureColor)
				processMaterialColor(line.substr(3).c_str(), &materials.at(materials.size() - 1));
		}else if (line.find("d ") == 0) {
			materials.at(materials.size() - 1).opacity = stod(line.substr(2).c_str()) * 0xFF;
			materials.at(materials.size() - 1).opacityOrg = materials.at(materials.size() - 1).opacity;
			processMaterialColorAlpha(stod(line.substr(2).c_str()), &materials.at(materials.size() - 1));
		}
		else if (line.find("map_Kd ") == 0) {
			checkTextureTypeInfo(materials.at(materials.size() - 1));
			switch (materials.at(materials.size() - 1).texType) {
				case TEX_RGBA16:
					size = processRGBA16Image(line.substr(7).c_str(), &materials.at(materials.size() - 1));
					break;
				case TEX_RGBA32:
					size = processRGBA32Image(line.substr(7).c_str(), &materials.at(materials.size() - 1));
					break;
				case TEX_I4:
					size = processIntensityImage(line.substr(7).c_str(), &materials.at(materials.size() - 1), 4);
					break;
				case TEX_I8:
					size = processIntensityImage(line.substr(7).c_str(), &materials.at(materials.size() - 1), 8);
					break;
				case TEX_IA4:
					size = processIntensityAlphaImage(line.substr(7).c_str(), &materials.at(materials.size() - 1), 4);
					break;
				case TEX_IA8:
					size = processIntensityAlphaImage(line.substr(7).c_str(), &materials.at(materials.size() - 1), 8);
					break;
				case TEX_IA16:
					size = processIntensityAlphaImage(line.substr(7).c_str(), &materials.at(materials.size() - 1), 16);
					break;
			}
		}
	}
	if (!materials.at(materials.size() - 1).isTextureCopy) {
		(&materials.at(materials.size() - 1))->offset = lastOffset;
		(&materials.at(materials.size() - 1))->size = size;
		if (verbose) printf("New texture '%s', size = 0x%X\n", materials.at(materials.size() - 1).name.c_str(), materials.at(materials.size() - 1).size);
	}if (materials.at(materials.size() - 1).enableTextureColor) {
		lastOffset += 0x10;
	}
	infile.close();
}

void processVertex(string str) {
	vector<string> splitXYZ = splitString(str, " ");
	vertex v;
	v.x = (stod(splitXYZ[0]) * modelScale) + xOff;
	v.y = (stod(splitXYZ[1]) * modelScale) + yOff;
	v.z = (stod(splitXYZ[2]) * modelScale) + zOff;
	verts.push_back(v);
}

void processUV(string str) {
	string u = str.substr(0, str.find(' '));
	string v = str.substr(u.length() + 1);
	texCord uv;
	uv.u = stod(u) * 32 * 32;
	uv.v = -(stod(v) * 32 * 32);
	uvs.push_back(uv);
}

void processNormal(string str) {
	vector<string> splitXYZ = splitString(str, " ");

	normal n;
	n.a = stod(splitXYZ[0]) * 0x7F;
	n.b = stod(splitXYZ[1]) * 0x7F;
	n.c = stod(splitXYZ[2]) * 0x7F;
	n.d = 0xFF;
	norms.push_back(n);
}

void processFaces() {
	for (face& f : faces) {
		
		vector<string> splitXYZ = splitString(f.data, " ");
		//cout << "Face: " << splitXYZ[0] << "--" << splitXYZ[1] << "--" << splitXYZ[2] << endl;
		vector<string> splitSubA = splitString(splitXYZ[0], "/");
		vector<string> splitSubB = splitString(splitXYZ[1], "/");
		vector<string> splitSubC = splitString(splitXYZ[2], "/");
		vertex va = verts[stoi(splitSubA[0]) - 1];
		texCord ta = uvs[stoi(splitSubA[1]) - 1];
		normal na = norms[stoi(splitSubA[2]) - 1];
		vertex vb = verts[stoi(splitSubB[0]) - 1];
		texCord tb = uvs[stoi(splitSubB[1]) - 1];
		normal nb = norms[stoi(splitSubA[2]) - 1];
		vertex vc = verts[stoi(splitSubC[0]) - 1];
		texCord tc = uvs[stoi(splitSubC[1]) - 1];
		normal nc = norms[stoi(splitSubA[2]) - 1];

		finalVertex fa, fb, fc;

		// Modify UV cordinates based on material.
		ta.u = ta.u * (float)(f.mat->texWidth / 32.0) - 0x10;
		ta.v = ta.v * (float)(f.mat->texHeight / 32.0) - 0x10;
		tb.u = tb.u * (float)(f.mat->texWidth / 32.0) - 0x10;
		tb.v = tb.v * (float)(f.mat->texHeight / 32.0) - 0x10;
		tc.u = tc.u * (float)(f.mat->texWidth / 32.0) - 0x10;
		tc.v = tc.v * (float)(f.mat->texHeight / 32.0) - 0x10;

		// Vertex Structure: xxxxyyyyzzzz0000uuuuvvvvrrggbbaa
		fa.data[0] = (va.x >> 8) & 0xFF; 
		fa.data[1] = va.x & 0xFF;
		fa.data[2] = (va.y >> 8) & 0xFF; 
		fa.data[3] = va.y & 0xFF;
		fa.data[4] = (va.z >> 8) & 0xFF; 
		fa.data[5] = va.z & 0xFF;
		fa.data[6] = 0; 
		fa.data[7] = 0;
		fa.data[8] = (ta.u >> 8) & 0xFF; 
		fa.data[9] = ta.u & 0xFF;
		fa.data[10] = (ta.v >> 8) & 0xFF; 
		fa.data[11] = ta.v & 0xFF;
		fa.data[12] = na.a; 
		fa.data[13] = na.b;
		fa.data[14] = na.c; 
		fa.data[15] = na.d;

		fb.data[0] = (vb.x >> 8) & 0xFF; 
		fb.data[1] = vb.x & 0xFF;
		fb.data[2] = (vb.y >> 8) & 0xFF; 
		fb.data[3] = vb.y & 0xFF;
		fb.data[4] = (vb.z >> 8) & 0xFF; 
		fb.data[5] = vb.z & 0xFF;
		fb.data[6] = 0; 
		fb.data[7] = 0;
		fb.data[8] = (tb.u >> 8) & 0xFF; 
		fb.data[9] = tb.u & 0xFF;
		fb.data[10] = (tb.v >> 8) & 0xFF; 
		fb.data[11] = tb.v & 0xFF;
		fb.data[12] = nb.a; 
		fb.data[13] = nb.b;
		fb.data[14] = nb.c; 
		fb.data[15] = nb.d;

		fc.data[0] = (vc.x >> 8) & 0xFF; 
		fc.data[1] = vc.x & 0xFF;
		fc.data[2] = (vc.y >> 8) & 0xFF; 
		fc.data[3] = vc.y & 0xFF;
		fc.data[4] = (vc.z >> 8) & 0xFF; 
		fc.data[5] = vc.z & 0xFF;
		fc.data[6] = 0; 
		fc.data[7] = 0;
		fc.data[8] = (tc.u >> 8) & 0xFF; 
		fc.data[9] = tc.u & 0xFF;
		fc.data[10] = (tc.v >> 8) & 0xFF; 
		fc.data[11] = tc.v & 0xFF;
		fc.data[12] = nc.a; 
		fc.data[13] = nc.b;
		fc.data[14] = nc.c; 
		fc.data[15] = nc.d;

		combinedVerts.push_back(fa);
		combinedVerts.push_back(fb);
		combinedVerts.push_back(fc);
	}
}


// Checks to see if both arrays are equal.
bool compareArrays(u8 * a, u8 * b, u32 size) {
	for (int i = 0; i < size; ++i)
		if (a[i] != b[i]) return false;
	return true;
}

void moveElementsInGroupUpward(fvGroup& grp, int amount, int start) {
	for (int i = 0; i < amount; i++) {
		//printf("Erasing element %d, size = %d: ", start + i, grp.fv.size());
		//printHexBytes(grp.fv[start].data, 16);
		grp.fv.erase(grp.fv.begin()+start);
		if (grp.size > 0) grp.size--;
	}
}

bool moveVertsBack(fvGroup& to, fvGroup& from) {
	//printf("to.size = %d, from.size = %d\n", to.size, from.size);
	//printHexBytes((u8*)to.indexList, to.numTri * 3);
	if (from.size < 3) return false;
	if (to.size < 14) {
		to.fv.push_back(from.fv[0]);
		to.fv.push_back(from.fv[1]);
		to.fv.push_back(from.fv[2]);
		to.indexList[to.numTri * 3] = to.size;
		to.indexList[to.numTri * 3 + 1] = to.size + 1;
		to.indexList[to.numTri * 3 + 2] = to.size + 2;
		//printf("Adding local verts: %d, %d, %d\n", to.indexList[to.numTri * 3], to.indexList[to.numTri * 3 + 1], to.indexList[to.numTri * 3 + 2]);
		to.size += 3;
		moveElementsInGroupUpward(from, 3, 0);
		to.numTri++;
		return true;
	}
	return false;
}

void updateIndexList(fvGroup& grp, u8 removed, u8 replaceWith) {
	for (int i = 0; i < grp.numTri * 3; i++) {
		if (grp.indexList[i] < removed) continue;
		if (grp.indexList[i] == removed)
			grp.indexList[i] = replaceWith;
		else
			grp.indexList[i]--;
	}
}

void updatePositions(int vs) {
	for (materialPosition& mp : matPos) {
		if (mp.vertStart <= vs) continue;
		if (mp.vertStart < 0x10) continue;
		mp.vertStart -= 0x10;
	}
}

void removeDuplicateVertices(u8 level) {
	if (level < 1) return;
	int dupCnt = 0;
	for (materialPosition& mp : matPos) {
		for (int g = 0; g < mp.numGroups; g++) {
			fvGroup& fvg = mp.fvg[g];
			if (fvg.size < 1) continue;
			//printf("group %d size = %d\n", g, fvg.size);
			for (int i = 0; i < fvg.size; i++) {
				for (int j = i+1; j < fvg.size; j++) {
					if (compareArrays(fvg.fv[i].data, fvg.fv[j].data, 16))
					{
						//printf("(%d) Two arrays matched! %d & %d\n", g, i, j);
						moveElementsInGroupUpward(fvg, 1, j);
						updateIndexList(fvg, j, i);
						updatePositions(mp.vertStart);
						dupCnt++;
					}
				}
			}
			if (g < mp.numGroups - 1 && level > 1) {
				if (moveVertsBack(mp.fvg[g], mp.fvg[g + 1])) g--;
			}
		}
	}
	if (verbose) printf("Removed %d duplicate vertices. Saved 0x%X bytes of data.\n", dupCnt, dupCnt * 0x10);
}

void makeVertexGroups() {
	int vs = 0;
	for (int i = 0; i < matPos.size(); i++) {
		matPos[i].vertStart = vs;
		int length = 0;
		if (i < matPos.size() - 1) 
			length = matPos.at(i + 1).position - matPos[i].position;
		else 
			length = currentFace - matPos[i].position;

		if (length % 5 == 0)
			matPos[i].numGroups = length / 5;
		else
			matPos[i].numGroups = (length / 5) + 1;

		for (int j = 0; j < matPos[i].numGroups; j++) {
			fvGroup newGroup;
			newGroup.numTri = 0;
			newGroup.size = 0;
			matPos[i].fvg.push_back(newGroup);
		}

		for (int g = 0; g < matPos[i].numGroups; g++) {
			int s = 5;
			if (g == matPos[i].numGroups - 1) s = length % 5;
			if (s == 0) s = 5;
			matPos[i].fvg[g].numTri = s;
			for (int j = 0; j < s; j++) {
				int from = (matPos[i].position + (j + g * 5)) * 3;
				matPos[i].fvg[g].fv.push_back(combinedVerts[from]);
				matPos[i].fvg[g].fv.push_back(combinedVerts[from+1]);
				matPos[i].fvg[g].fv.push_back(combinedVerts[from+2]);
				matPos[i].fvg[g].indexList[j * 3] = j * 3;
				matPos[i].fvg[g].indexList[j * 3 + 1] = j * 3 + 1;
				matPos[i].fvg[g].indexList[j * 3 + 2] = j * 3 + 2;
			}
			matPos[i].fvg[g].size = matPos[i].fvg[g].numTri * 3;
			vs += matPos[i].fvg[g].size * 0x10;
			//printf("vs = 0x%X\n", vs);
		}
	}
}

void addMaterialPosition(string str) {
	for (auto &m : materials) {
		if (strcmp(m.name.c_str(), str.c_str()) == 0) {
			if (verbose) printf("Adding positions! %d & %d\n", lastPos, currentFace);
			materialPosition mp;
			mp.position = currentFace;
			mp.mat = &m;
			currentMaterial = &m;
			mp.cLen = 0;
			matPos.push_back(mp);
			lastPos = currentFace;
			return;
		}
	}
}
void processLine(string str) {
	if (str.find("mtllib ") == 0) {
		if (verbose) cout << "Found material lib: " << str << endl;
		processMaterialLib(string(curObjPath).append(str.substr(7)));
	} else if (str.find("usemtl ") == 0) {
		addMaterialPosition(str.substr(7));
	} else if (str.find("v ") == 0) {
		processVertex(str.substr(2));
	} else if (str.find("vt ") == 0) {
		processUV(str.substr(3));
	} else if (str.find("vn ") == 0) {
		processNormal(str.substr(3));
	} else if (str.find("f ") == 0) {
		face f;
		f.data = str.substr(2);
		f.mat = currentMaterial;
		faces.push_back(f);
		currentFace++;
	}
}

f3d strF3D(string str) {
	f3d cmd;
	vector<string> b = splitString(str, " ");
	for (int i = 0; i < b.size(); i++) {
		cmd.data[i] = strtoul(b.at(i).c_str(), 0, 16);
	}
	return cmd;
}

geoCmd strGeo(string str) {
	geoCmd cmd;
	vector<string> b = splitString(str, " ");
	for (int i = 0; i < b.size(); i++) {
		cmd.data[i] = strtoul(b.at(i).c_str(), 0, 16);
	}
	cmd.len = b.size();
	return cmd;
}

genData strGen(string str) {
	genData cmd;
	vector<string> b = splitString(trim(str), " ");
	for (int i = 0; i < b.size(); i++) {
		cmd.data[i] = strtoul(b.at(i).c_str(), 0, 16);
	}
	cmd.len = b.size();
	return cmd;
}

void resetVariables() {
	matPos.clear();
	verts.clear();
	norms.clear();
	uvs.clear();
	materials.clear();
	faces.clear();
	combinedVerts.clear();
	currentFace = 0;
	lastPos = 0;
	lastOffset = 0;
	lastTextureType = NOT_DEFINED;
}

void addFogStart(vector<f3d>& cmds, bool alpha) {
	cmds.push_back(strF3D("BA 00 14 02 00 10 00 00"));
	char cmdF8[24];
	sprintf(cmdF8, "F8 00 00 00 %02X %02X %02X FF", fogRed, fogGreen, fogBlue);
	cmds.push_back(strF3D(cmdF8));
	if(alpha) // If texture has any alpha bits
		cmds.push_back(strF3D("B9 00 03 1D C8 11 30 78"));
	else // solid texture
		cmds.push_back(strF3D("B9 00 03 1D C8 11 20 78"));
	switch (fogType) {
		case FOG_SUBTLE_1:
			cmds.push_back(strF3D("BC 00 00 08 19 00 E8 00"));
			break;
		case FOG_SUBTLE_2:
			cmds.push_back(strF3D("BC 00 00 08 12 00 F0 00"));
			break;
		case FOG_MODERATE_1:
			cmds.push_back(strF3D("BC 00 00 08 0E 49 F2 B7"));
			break;
		case FOG_MODERATE_2:
			cmds.push_back(strF3D("BC 00 00 08 0C 80 F4 80"));
			break;
		case FOG_MODERATE_3:
			cmds.push_back(strF3D("BC 00 00 08 0A 00 F7 00"));
			break;
		case FOG_MODERATE_4:
			cmds.push_back(strF3D("BC 00 00 08 08 55 F8 AB"));
			break;
		case FOG_INTENSE:
			cmds.push_back(strF3D("BC 00 00 08 07 24 F9 DC"));
			break;
		case FOG_VERY_INTENSE:
			cmds.push_back(strF3D("BC 00 00 08 05 00 FC 00"));
			break;
		case FOG_HARDCORE:
			cmds.push_back(strF3D("BC 00 00 08 02 50 FF 00"));
			break;
	}
	cmds.push_back(strF3D("B7 00 00 00 00 01 00 00"));
}

void addFogEnd(vector<f3d>& cmds) {
	cmds.push_back(strF3D("BA 00 14 02 00 00 00 00"));
	cmds.push_back(strF3D("B9 00 03 1D 00 44 30 78"));
	cmds.push_back(strF3D("B6 00 00 00 00 01 00 00"));
}

void addCmd03(vector<f3d>& cmds, material& mat, u32 addOffset) {
	if (mat.type == COLOR_SOLID) {
		u32 off = startSegOffset + mat.offset + 0x10;
		char cmd[24];
		sprintf(cmd, "03 86 00 10 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		off = startSegOffset + mat.offset + 0x18;
		sprintf(cmd, "03 88 00 10 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
	}
	else {
		u32 off = startSegOffset;
		if (mat.enableTextureColor) 
			off = startSegOffset + addOffset + mat.texColOffset;
		if (verbose) printf("CMD 03: (%s) texColOffset = 0x%X\n", mat.name.c_str(), mat.texColOffset);
		//printf("off (Color texture) = 0x%X\n", off);
		char cmd[24];
		sprintf(cmd, "03 86 00 10 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		off = startSegOffset + 8;
		if (mat.enableTextureColor)
			off = startSegOffset + addOffset + mat.texColOffset + 0x8;
		sprintf(cmd, "03 88 00 10 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
	}
}

void addTriCmds(vector<f3d>& cmds, material& mat, fvGroup& grp, int offset) {
	if (grp.size < 3) return;
	u32 off = startSegOffset + offset;
	int amount = grp.size * 0x10;
	//printf("Group size = 0x%X\n", grp.size);
	//printf("04: offset = 0x%X, amount = 0x%X\n", off, amount);
	//printHexBytes((u8*)grp.indexList, grp.numTri * 3);

	char cmd[24];
	sprintf(cmd, "04 %X 00 %X %X %X %X %X", amount - 0x10 & 0xFF, amount & 0xFF, curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
	cmds.push_back(strF3D(cmd));
	for (int i = 0; i < grp.numTri; i++) {
		char cmd[24];
		u8 a = grp.indexList[i * 3] * 0xA;
		u8 b = grp.indexList[i * 3 + 1] * 0xA;
		u8 c = grp.indexList[i * 3 + 2] * 0xA;

		sprintf(cmd, "BF 00 00 00 00 %X %X %X", a, b, c);
		cmds.push_back(strF3D(cmd));
	}
}

void addCmdF2(vector<f3d>& cmds, material& mat) {
	u16 width = ((mat.texWidth - 1) << 2) & 0xFFF;
	u16 height = ((mat.texHeight - 1) << 2) & 0xFFF;
	u32 data = (width << 12) | height;
	char cmd[24];
	sprintf(cmd, "F2 00 00 00 00 %X %X %X", (data >> 16) & 0xFF, (data >> 8) & 0xFF, data & 0xFF);
	cmds.push_back(strF3D(cmd));
}

void addCmdF3(vector<f3d>& cmds, material& mat) {
	u32 numTexels = ((mat.texWidth * mat.texHeight + getTexelIncrement(mat.texType)) >> getTexelShift(mat.texType)) - 1;
	if (verbose) printf("F3: %d,%d,%d,%d numTexels = 0x%X \n", mat.texWidth, mat.texHeight, getTexelIncrement(mat.texType), getTexelShift(mat.texType), numTexels);
	int bpt = bytesPerType(mat.texType);
	u32 tl;
	if(bpt)
		tl = CALC_DXT(mat.texWidth, bpt) & 0xFFF;
	else
		tl = CALC_DXT_4b(mat.texWidth) & 0xFFF;
	
	u32 lower = ((numTexels << 12) | tl) & 0x00FFFFFF;
	
	char cmd[24];
	sprintf(cmd, "F3 00 00 00 07 %X %X %X", (lower >> 16) & 0xFF, (lower >> 8) & 0xFF, lower & 0xFF);
	cmds.push_back(strF3D(cmd));
}


u8 getTypeFromMaterial(material& mat) {
	switch (mat.texType) {
		case TEX_I4:
			return 0x80;
		case TEX_I8:
			return 0x88;
		case TEX_IA4:
			return 0x60;
		case TEX_IA8:
			return 0x68;
		case TEX_IA16:
			return 0x70;
		case TEX_RGBA32:
			return 0x18;
		default:
			return 0x10; // RGBA16
	}
}

void addCmdF5(vector<f3d>& cmds, material& mat, bool first) {
	u8 type = getTypeFromMaterial(mat);
	char cmd[24];
	if (first) {
		if (mat.hasTexture) {
			if (mat.texType == TEX_I4) type = 0x90;
			else if (mat.texType == TEX_IA4 || mat.texType == TEX_IA8) type = 0x70;
			sprintf(cmd, "F5 %X 00 00 07 00 00 00", type);
			cmds.push_back(strF3D(cmd));
		}
	} else {
		if (mat.hasTexture) {
			float lineScale = 1.0f;
			u8 bpt = bytesPerType(mat.texType);
			if (bpt) 
				lineScale = bpt / 4.0;
			else
				lineScale = 0.125f;
			if (mat.texType == TEX_RGBA32) lineScale /= 2;
			u16 line = (u16)(mat.texWidth * lineScale) & 0x1FF;
			u32 upper = ((type << 16) | (line << 8)) & 0x00FFFFFF;
			u8 maskS = (u8)ceil(log2(mat.texWidth)) & 0xF;
			u8 maskT = (u8)ceil(log2(mat.texHeight)) & 0xF;
			u32 lower = ((maskT << 14) | (maskS << 4)) & 0x00FFFFFF;
			sprintf(cmd, "F5 %X %X %X 00 %X %X %X", 
			(upper >> 16) & 0xFF, (upper >> 8) & 0xFF, upper & 0xFF,
			(lower >> 16) & 0xFF, (lower >> 8) & 0xFF, lower & 0xFF);
			if (verbose) printf("F5 CMD: %s\n", cmd);
			cmds.push_back(strF3D(cmd));
		}
	}
}

void addColorCmdFB(vector<f3d>& cmds, material& mat) {
	char cmd[24];
	u8 r = (mat.color >> 24) & 0xFF;
	u8 g = (mat.color >> 16) & 0xFF;
	u8 b = (mat.color >> 8) & 0xFF;
	sprintf(cmd, "FB 00 00 00 %X %X %X %X", r, g, b, mat.opacity);
	cmds.push_back(strF3D(cmd));
}

void addCmdFC(vector<f3d>& cmds, material& mat) {
	if (mat.hasTexture) {
		if(mat.texType == TEX_RGBA32)
			cmds.push_back(strF3D("FC 11 96 23 FF 2F FF FF"));
		else if (mat.texType == TEX_IA4 || mat.texType == TEX_IA8 || mat.texType == TEX_IA16)
			cmds.push_back(strF3D("FC 12 9A 25 FF 37 FF FF"));
		else if (mat.texType == TEX_I4 || mat.texType == TEX_I8)
			if(mat.enableAlphaMask)
				cmds.push_back(strF3D("FC 12 7E A0 FF FF F3 F8"));
			else
				cmds.push_back(strF3D("FC 30 B2 61 FF FF FF FF"));
		else if (mat.hasTextureAlpha)
			if(enableFog)
				cmds.push_back(strF3D("FC FF FF FF FF FC F2 38"));
			else
				cmds.push_back(strF3D("FC 12 18 24 FF 33 FF FF"));
		else if (mat.type == TEXTURE_TRANSPARENT) {
				cmds.push_back(strF3D("FC 12 2E 24 FF FF FB FD"));
		}
		else
			if(enableFog)
				cmds.push_back(strF3D("FC 12 7F FF FF FF F8 38"));
			else
				cmds.push_back(strF3D("FC 12 7E 24 FF FF F9 FC"));
	}
	else
		if(mat.type == COLOR_TRANSPARENT)
			cmds.push_back(strF3D("FC FF FF FF FF FE FB FD"));
		else
			cmds.push_back(strF3D("FC FF FF FF FF FE 7B 3D"));
}

void addCmdFD(vector<f3d>& cmds, material& mat) {
	if (mat.hasTexture) {
		u32 off = startSegOffset + mat.offset + 0x10;
		char cmd[24];
		u8 type = getTypeFromMaterial(mat);
		if (mat.texType == TEX_I4) type = 0x90;
		else if (mat.texType == TEX_IA4 || mat.texType == TEX_IA8) type = 0x70;
		sprintf(cmd, "FD %X 00 00 %X %X %X %X", type, curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
	}
}

void fillColorData(u8* colorData, material& mt, float darkMult) {
	u8 lr, lg, lb, a;
	u16 dr, dg, db;
	lr = (mt.color >> 24) & 0xFF;
	lg = (mt.color >> 16) & 0xFF;
	lb = (mt.color >> 8) & 0xFF;
	dr = lr * darkMult;
	dg = lg * darkMult;
	db = lb * darkMult;
	if (dr > 0xFF) dr = 0xFF;
	if (dg > 0xFF) dg = 0xFF;
	if (db > 0xFF) db = 0xFF;
	a = mt.color & 0xFF;
	if (verbose) printf("Color: r=0x%X g=0x%X b=0x%X a=0x%X\n",lr,lg,lb,a);
	colorData[0] = lr;
	colorData[1] = lg;
	colorData[2] = lb;
	colorData[3] = a;
	colorData[4] = lr;
	colorData[5] = lg;
	colorData[6] = lb;
	colorData[7] = a;
	colorData[8] = dr;
	colorData[9] = dg;
	colorData[10] = db;
	colorData[11] = a;
	colorData[12] = dr;
	colorData[13] = dg;
	colorData[14] = db;
	colorData[15] = a;
}

void importOBJ(const char* objPath, const char* col_data) {
	printf("Importing '%s'\n", objPath);

	if (currentPreName != NULL) addObjPtr((char*)currentPreName->c_str());
	addObjPtr((char*)objPath);

	string path = objPath;
	int last = path.find_last_of('/');
	if (last != string::npos)
		curObjPath = path.substr(0, last+1);
	else
		curObjPath = "";

	string line;
	ifstream infile;
	infile.open(objPath);
	while (safeGetline(infile, line)) {
		processLine(line);
	}
	centerVertices();
	processFaces();
	makeVertexGroups();
	removeDuplicateVertices(reduceVertLevel);
	infile.close();
	if (curSegOffset % 8 != 0) curSegOffset += 8 - (curSegOffset % 8); // DEBUG (Add padding)

	if (currentPreName != NULL) addPtr((char*)currentPreName->c_str(), PTR_START, (curSeg << 24) | curSegOffset);
	addPtr((char*)objPath, PTR_START, (curSeg << 24) | curSegOffset);
	int importStart = romPos + curSegOffset;
	printf("importStart: 0x%X\n", importStart);
	curSegOffset += 0x10;
	for (material& mt : materials) {
		//printf("material '%s' offset = 0x%x, size = 0x%x\n", mt.name.c_str(), mt.offset, mt.size);
		if (!mt.isTextureCopy) curSegOffset += mt.size;
		if (mt.enableTextureColor) {
			mt.texColOffset = curSegOffset;
			if (verbose) printf("Texture '%s' Color: 0x%X\n", mt.name.c_str(), mt.color);
			curSegOffset += 0x10;
		}
	}

	int startVerts = curSegOffset;
	for (materialPosition& mp : matPos) {
		for (int g = 0; g < mp.numGroups; g++) {
			if (mp.fvg[g].size < 1) continue;
			curSegOffset += mp.fvg[g].size * 0x10;
		}
	}

	// We need to seperate the model into at most 3 seperate DLs.
	vector<f3d> solidCmds; // For textures/color with no transparency in them. (Layer 1)
	vector<f3d> alphaCmds; // For textures that have atleast 1 alpha bit in them. (Layer 4)
	vector<f3d> transCmds; // For textures/colors that have transparency in them. (Layer 5)

	printf("F3D solid starts at 0x%X\n", curSegOffset);
	if (currentPreName != NULL) addPtr((char*)currentPreName->c_str(), PTR_SOLID, (curSeg << 24) | curSegOffset);
	addPtr((char*)objPath, PTR_SOLID, (curSeg << 24) | curSegOffset);
	solidCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("B7 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("BB 00 00 01 FF FF FF FF"));
	solidCmds.push_back(strF3D("E8 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
	if (enableFog) addFogStart(solidCmds, false);
	for (int i = 0; i < matPos.size(); ++i) {
		materialPosition mp = matPos.at(i);
		//printf("Material '%s'\n", mp.mat->name.c_str());
		if (mp.mat->hasTextureAlpha || mp.mat->hasTransparency) continue;
		if (mp.mat->enableGeoMode) {
			solidCmds.push_back(strF3D("B6 00 00 00 FF FF FF FF"));
			//printf("geoMode for '%s':0x%X\n", mp.mat->name.c_str(), mp.mat->geoMode);
			char cmd[24];
			sprintf(cmd, "B7 00 00 00 %X %X %X %X",
				(mp.mat->geoMode >> 24) & 0xFF,
				(mp.mat->geoMode >> 16) & 0xFF,
				(mp.mat->geoMode >> 8) & 0xFF,
				mp.mat->geoMode & 0xFF
				);
			solidCmds.push_back(strF3D(cmd));
		}

		if (mp.mat->hasTexture) {
			addCmdFD(solidCmds, *mp.mat);
			addCmdF5(solidCmds, *mp.mat, true);
			solidCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
			addCmdF3(solidCmds, *mp.mat);
			solidCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
			addCmdF5(solidCmds, *mp.mat, false);
			addCmdF2(solidCmds, *mp.mat);
		}

		//printf("Current type: %d\n", mp.mat->type);
		//printf("Comparing Types (SOLID) %d != %d\n", lastTextureType, mp.mat->type);
		if (lastTextureType != mp.mat->type) {
			addCmdFC(solidCmds, *mp.mat);
			addCmd03(solidCmds, *mp.mat, importStart - romPos);
			lastTextureType = mp.mat->type;
		}
		else {
			if (i > 0) {
				if (matPos.at(i - 1).mat->enableTextureColor && mp.mat->enableTextureColor) {
					if(matPos.at(i - 1).mat->color != mp.mat->color)
						addCmd03(solidCmds, *mp.mat, importStart - romPos);
				}
				else if (mp.mat->enableTextureColor) {
					addCmd03(solidCmds, *mp.mat, importStart - romPos);
				}
			}
			if (lastTextureType == COLOR_SOLID && mp.mat->type == COLOR_SOLID) {
				addCmd03(solidCmds, *mp.mat, importStart-romPos);
			}
		}

		//printf("Material '%s': %d, vertStart = %d\n", mp.mat->name.c_str(), mp.position, mp.vertStart);
		int grpOff = 0;
		for (int i = 0; i < mp.numGroups; i++) {
			addTriCmds(solidCmds, *mp.mat, mp.fvg[i], startVerts + (mp.vertStart + grpOff));
			grpOff += mp.fvg[i].size * 0x10;
		}

		if (mp.mat->enableGeoMode) {
			if(i+1 < matPos.size()) if (matPos.at(i + 1).mat->enableGeoMode) continue;
			solidCmds.push_back(strF3D("B6 00 00 00 FF FF FF FF"));
			solidCmds.push_back(strF3D("B7 00 00 00 00 02 20 05"));
		}
	}
	if (enableFog) addFogEnd(solidCmds);
	solidCmds.push_back(strF3D("BB 00 00 00 FF FF FF FF"));
	solidCmds.push_back(strF3D("B8 00 00 00 00 00 00 00"));
	//for (f3d cmd : solidCmds) printHexBytes(cmd.data, 8);

	curSegOffset += solidCmds.size() * 8;

	if (createAlphaDL) {
		printf("F3D alpha starts at 0x%X\n", curSegOffset);
		if (currentPreName != NULL) addPtr((char*)currentPreName->c_str(), PTR_ALPHA, (curSeg << 24) | curSegOffset);
		addPtr((char*)objPath, PTR_ALPHA, (curSeg << 24) | curSegOffset);
		alphaCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
		if (enableFog) alphaCmds.push_back(strF3D("B9 00 02 01 00 00 00 00"));
		alphaCmds.push_back(strF3D("B7 00 00 00 00 00 00 00"));
		alphaCmds.push_back(strF3D("BB 00 00 01 FF FF FF FF"));
		alphaCmds.push_back(strF3D("E8 00 00 00 00 00 00 00"));
		alphaCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
		if (enableFog) addFogStart(alphaCmds, true);
		for (int i = 0; i < matPos.size(); ++i) {
			materialPosition mp = matPos.at(i);
			if (!mp.mat->hasTextureAlpha || mp.mat->hasTransparency) continue;
			//printf("Comparing Types for texture '%s' (ALPHA) %d != %d\n",mp.mat->name.c_str(), lastTextureType, mp.mat->type);
			if (mp.mat->enableGeoMode) {
				alphaCmds.push_back(strF3D("B6 00 00 00 FF FF FF FF"));
				char cmd[24];
				sprintf(cmd, "B7 00 00 00 %X %X %X %X", 
					(mp.mat->geoMode >> 24) & 0xFF,
					(mp.mat->geoMode >> 16) & 0xFF,
					(mp.mat->geoMode >> 8) & 0xFF,
					mp.mat->geoMode & 0xFF
				);
				alphaCmds.push_back(strF3D(cmd));
			}
			if (lastTextureType != mp.mat->type) {
				//printf("Current type: %d\n", mp.mat->type);
				addCmdFC(alphaCmds, *mp.mat);
				addCmd03(alphaCmds, *mp.mat, importStart - romPos);
				lastTextureType = mp.mat->type;
			}
			if (mp.mat->hasTexture) {
				addCmdFD(alphaCmds, *mp.mat);
				addCmdF5(alphaCmds, *mp.mat, true);
				alphaCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
				addCmdF3(alphaCmds, *mp.mat);
				alphaCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
				addCmdF5(alphaCmds, *mp.mat, false);
				addCmdF2(alphaCmds, *mp.mat);
			}
			//printf("Material '%s': %d \n", mp.mat->name.c_str(), mp.position);
			int grpOff = 0;
			for (int i = 0; i < mp.numGroups; i++) {
				addTriCmds(alphaCmds, *mp.mat, mp.fvg[i], startVerts + (mp.vertStart + grpOff));
				grpOff += mp.fvg[i].size * 0x10;
			}
			if (mp.mat->enableGeoMode) {
				alphaCmds.push_back(strF3D("B6 00 00 00 FF FF FF FF"));
				alphaCmds.push_back(strF3D("B7 00 00 00 00 02 20 05"));
			}
		}
		if (enableFog) addFogEnd(alphaCmds);
		alphaCmds.push_back(strF3D("FC FF FF FF FF FE 79 3C"));
		alphaCmds.push_back(strF3D("BB 00 00 00 FF FF FF FF"));
		alphaCmds.push_back(strF3D("B8 00 00 00 00 00 00 00"));
		curSegOffset += alphaCmds.size() * 8;
		//for (f3d cmd : alphaCmds) printHexBytes(cmd.data, 8);
	}

	if (createTransDL) {
		bool resetBF = false;
		material * lastMat = NULL;
		printf("F3D transparency starts at 0x%X\n", curSegOffset);
		if (currentPreName != NULL) addPtr((char*)currentPreName->c_str(), PTR_TRANS, (curSeg << 24) | curSegOffset);
		addPtr((char*)objPath, PTR_TRANS, (curSeg << 24) | curSegOffset);
		transCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
		transCmds.push_back(strF3D("B7 00 00 00 00 00 00 00"));
		transCmds.push_back(strF3D("BB 00 00 01 FF FF FF FF"));
		transCmds.push_back(strF3D("E8 00 00 00 00 00 00 00"));
		transCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));

		//transCmds.push_back(strF3D("EF 00 2C 30 00 50 42 41"));


		for (int i = 0; i < matPos.size(); ++i) {
			materialPosition mp = matPos.at(i);
			if (mp.mat->hasTextureAlpha || !mp.mat->hasTransparency) continue;

			//printf("Comparing Types for texture '%s' (TRANS) %d != %d\n", mp.mat->name.c_str(), lastTextureType, mp.mat->type);
			if (lastTextureType != mp.mat->type) {
				if (mp.mat->type == TEXTURE_TRANSPARENT) {
					if (mp.mat->opacityOrg < 0xFF) {
						char cmd[24];
						sprintf(cmd, "FB 00 00 00 FF FF FF %X", mp.mat->opacityOrg);
						transCmds.push_back(strF3D(cmd));
						resetBF = true;
					}
				}
				//printf("Current trans mat: %s\n", mp.mat->name.c_str());
				addCmdFC(transCmds, *mp.mat);
				addCmd03(transCmds, *mp.mat, importStart - romPos);
				if (mp.mat->type == COLOR_TRANSPARENT) {
					addColorCmdFB(transCmds, *mp.mat);
					resetBF = true;
				}
				lastTextureType = mp.mat->type;
			}
			else {
				if (i > 0) {
					if (matPos.at(i - 1).mat->enableTextureColor && mp.mat->enableTextureColor) {
						if (matPos.at(i - 1).mat->color != mp.mat->color)
							addCmd03(transCmds, *mp.mat, importStart - romPos);
					} else if (mp.mat->enableTextureColor) {
						addCmd03(transCmds, *mp.mat, importStart - romPos);
					}
				}
				if (lastTextureType == COLOR_TRANSPARENT && mp.mat->type == COLOR_TRANSPARENT) {
					if (mp.mat != lastMat) {
						addColorCmdFB(transCmds, *mp.mat);
						resetBF = true;
					}
				}
			}
			if (mp.mat->hasTexture) {
				addCmdFD(transCmds, *mp.mat);
				addCmdF5(transCmds, *mp.mat, true);
				transCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
				addCmdF3(transCmds, *mp.mat);
				transCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
				addCmdF5(transCmds, *mp.mat, false);
				addCmdF2(transCmds, *mp.mat);
			}
			//printf("Material '%s': %d \n", mp.mat->name.c_str(), mp.position);
			int grpOff = 0;
			for (int i = 0; i < mp.numGroups; i++) {
				addTriCmds(transCmds, *mp.mat, mp.fvg[i], startVerts + (mp.vertStart + grpOff));
				grpOff += mp.fvg[i].size * 0x10;
			}
			lastMat = mp.mat;
		}
		if (resetBF) transCmds.push_back(strF3D("FB 00 00 00 FF FF FF FF"));

		transCmds.push_back(strF3D("FC FF FF FF FF FE 79 3C"));
		transCmds.push_back(strF3D("BB 00 00 00 FF FF FF FF"));
		transCmds.push_back(strF3D("B8 00 00 00 00 00 00 00"));
		curSegOffset += transCmds.size() * 8;
		//for (f3d cmd : transCmds) printHexBytes(cmd.data, 8);
	}

	int size = curSegOffset - (importStart-romPos);

	u8 * importData = (u8*)malloc(size);
	memcpy(importData, defaultColor, 0x10);
	int curOff = 0x10;
	for (material mt : materials) {
		if (verbose) printf("material '%s' offset = 0x%x, size = 0x%x\n", mt.name.c_str(), mt.offset, mt.size);
		if (mt.hasTexture) {
			if (!mt.isTextureCopy) {
				memcpy(importData + curOff, (u8*)&textureBank.at(mt.texture).data[0], mt.size);
				curOff += mt.size;
			}
			if (mt.enableTextureColor) {
				u8 colorData[16];
				fillColorData(colorData, mt, mt.texColDark);
				//printHexBytes(colorData, 16);
				memcpy(importData + curOff, colorData, 0x10);
				curOff += 0x10;
			}
		}
		else {
			u8 colorData[16];
			fillColorData(colorData, mt, 0.8f);
			memcpy(importData + curOff, colorData, 0x10);
			curOff += 0x10;
		}
	}

	for (materialPosition mp : matPos) {
		//printf("material '%s'\n", mp.mat->name.c_str());
		for (int g = 0; g < mp.numGroups; g++) {
			if (mp.fvg[g].size < 1) continue;
			for (int i = 0; i < mp.fvg[g].size; i++){
				memcpy(importData + curOff, mp.fvg[g].fv[i].data, 0x10);
				//printHexBytes(mp.fvg[g].fv[i].data, 0x10);
				curOff += 0x10;
			}
		}
	}
	for (f3d cmd : solidCmds) {
		memcpy(importData + curOff, cmd.data, 8);
		curOff += 8;
	}

	if (createAlphaDL) {
		for (f3d cmd : alphaCmds) {
			memcpy(importData + curOff, cmd.data, 8);
			curOff += 8;
		}
	}

	if (createTransDL) {
		for (f3d cmd : transCmds) {
			memcpy(importData + curOff, cmd.data, 8);
			curOff += 8;
		}
	}

	if (curImp->curPos + size <= curImp->limit) {
		memcpy(curImp->data + curImp->curPos, importData, size);
		curImp->curPos += size;
	} else {
		err.id = 100;
		err.message = "Import data limit has been exceeded!";
	}
	if(col_data != NULL && err.id == 0) importOBJCollision(objPath, col_data, true);
	printf("Total Size = 0x%X\n", curSegOffset - (importStart - romPos));

	resetVariables();
	currentPreName = NULL;
}

void importOBJCollision(const char* objPath, const char* col_data, bool continued) {
	if (!continued) {
		addObjPtr((char*)objPath);
		resetVariables();

		string path = objPath;
		int last = path.find_last_of('/');
		if (last != string::npos)
			curObjPath = path.substr(0, last + 1);
		else
			curObjPath = "";

		string line;
		ifstream infile;
		infile.open(objPath);
		while (safeGetline(infile, line)) {
			processLine(line);
		}
		centerVertices();
		infile.close();
	}

	vector<string> col_entries = splitString(col_data, ",");
	for (material& mt : materials) {
		for (string ent : col_entries) {
			if (ent.find(mt.name.c_str()) == 0) {
				vector<string> cd = splitString(ent, ":");
				u16 val;
				if (cd.at(1).find("0x") == 0)
					val = stoi(cd.at(1).substr(2), 0, 16);
				else if (cd.at(1).find("$") == 0)
					val = stoi(cd.at(1).substr(1), 0, 16);
				else
					val = stoi(cd.at(1));
				mt.collision = val;
			}
		}
	}

	for (int i = 0; i < matPos.size(); ++i) {
		if (i >= matPos.size()) continue;
		materialPosition * mp = &matPos.at(i);

		int length;
		if (i < matPos.size() - 1) length = matPos.at(i + 1).position - mp->position;
		else length = currentFace - mp->position;
		mp->cLen = length;

		if (i > 0) {
			if (mp->mat->collision == matPos.at(i - 1).mat->collision) {
				matPos.at(i - 1).cLen += mp->cLen;
				matPos.erase(matPos.begin() + i);
				i--;
			}
		}
	}
	int collisionStart = curSegOffset;
	vector<u16> collision;
	collision.push_back(0x0040);
	if(!enableDeathFloor)
		collision.push_back(verts.size());
	else
		collision.push_back(verts.size() + 4);
	curSegOffset += 4;

	for (vertex v : verts) {
		collision.push_back(v.x);
		collision.push_back(v.y);
		collision.push_back(v.z);
		curSegOffset += 6;
	}

	if (enableDeathFloor) {
		collision.push_back(dfx1);
		collision.push_back(dfy);
		collision.push_back(dfz1);
		collision.push_back(dfx2);
		collision.push_back(dfy);
		collision.push_back(dfz2);
		collision.push_back(dfx2);
		collision.push_back(dfy);
		collision.push_back(dfz1);
		collision.push_back(dfx1);
		collision.push_back(dfy);
		collision.push_back(dfz2);
		curSegOffset += 24;
	}

	for (int i = 0; i < matPos.size(); ++i) {
		materialPosition * mp = &matPos.at(i);
		if (mp->position < 0 || mp->cLen < 1) continue;
		//printf("mat '%s': pos = %d, len = %d, collision = %d\n", mp->mat->name.c_str(), mp->position, mp->cLen, mp->mat->collision);
		curSegOffset += 4;
		collision.push_back(mp->mat->collision);
		collision.push_back(mp->cLen);

		for (int j = 0; j < mp->cLen; j++) {
			vector<string> splitXYZ = splitString(faces.at(mp->position + j).data, " ");
			vector<string> splitSubA = splitString(splitXYZ[0], "/");
			vector<string> splitSubB = splitString(splitXYZ[1], "/");
			vector<string> splitSubC = splitString(splitXYZ[2], "/");
			
			collision.push_back(stoi(splitSubA[0]) - 1);
			collision.push_back(stoi(splitSubB[0]) - 1);
			collision.push_back(stoi(splitSubC[0]) - 1);
			curSegOffset += 6;
		}
	}
	if (enableDeathFloor) {
		collision.push_back(0x000A);
		collision.push_back(0x0002);
		collision.push_back(verts.size());
		collision.push_back(verts.size() + 1);
		collision.push_back(verts.size() + 2);
		collision.push_back(verts.size());
		collision.push_back(verts.size() + 3);
		collision.push_back(verts.size() + 1);
		curSegOffset += 16;
	}
	collision.push_back(0x0041);

	if (waterBoxes.size() > 0) {
		collision.push_back(0x0044);
		collision.push_back(waterBoxes.size());
		curSegOffset += 4;
		for (waterBox& wb : waterBoxes) {
			collision.push_back(wb.index);
			collision.push_back(wb.minx);
			collision.push_back(wb.minz);
			collision.push_back(wb.maxx);
			collision.push_back(wb.maxz);
			collision.push_back(wb.y);
			curSegOffset += 12;
		}
	}

	collision.push_back(0x0042);
	curSegOffset += 4;

	if(currentPreName != NULL) addPtr((char*)currentPreName->c_str(), PTR_COL, (curSeg << 24) | collisionStart);
	addPtr((char*)objPath, PTR_COL, (curSeg << 24) | collisionStart);

	//printHexShorts((u16*)&collision[0], collision.size());
	printf("Collision starts at 0x%X\n", collisionStart);

	if(checkLittleEndian)
		for (u16& col : collision) col = BYTE_SWAP_16(col);
	if (verbose) printf("Collision length = %X\n",collision.size()*2);

	if (curImp->curPos + collision.size()*2 <= curImp->limit) {
		memcpy(curImp->data + curImp->curPos, (u8*)&collision[0], collision.size() * 2);
		curImp->curPos += collision.size() * 2;
	}
	else {
		err.id = 100;
		err.message = "Import data limit has been exceeded!";
	}

	currentPreName = NULL;
}

void write16(u8* arr, u32 offset, u16 value){
	arr[offset + 0] = (value >> 8) & 0xFF;
	arr[offset + 1] = value & 0xFF;
}

void write32(u8* arr, u32 offset, u32 value) {
	arr[offset + 0] = (value >> 24) & 0xFF;
	arr[offset + 1] = (value >> 16) & 0xFF;
	arr[offset + 2] = (value >> 8) & 0xFF;
	arr[offset + 3] = value & 0xFF;
}

void writeMany(u8* arr, u32 offset, u32 amount, u8 value) {
	//printf("# of 0x%X = %d\n", value, amount);
	for (int i = 0; i < amount; i++) 
		arr[offset + i] = value;
}

// This needs improving.
void importWaterBoxData(int offset) {
	u8 * importData = (u8*)malloc(offset + waterBoxes.size() * 0x20);

	u32 BoxIndex = 0;
	char cmd[256];
	int cur = 0;
	for (waterBox& wb: waterBoxes)
	{
		if (wb.type == 0) // regular water
		{
			wb.index = BoxIndex;
			write16(importData, cur, BoxIndex);
			u32 ind = 0x19001C00 + (BoxIndex * 0x20);
			write32(importData, cur + 4, ind);
			wb.pointer = (u32*)(0x1C00 + (BoxIndex * 0x20));
			BoxIndex++;
			cur += 8;
		}

	}
	write16(importData, cur, 0xFFFF); // end list
	cur += 2;
	writeMany(importData, cur, 0x50 - cur, 0x01); // fill gap with 0x01
	cur += 0x50 - cur;
	int ToxicIndex = 0x32;
	for (waterBox& wb : waterBoxes)
	{
		//printf("Type: %d\n", wb.type);
		if (wb.type == 1) // toxic haze
		{
			wb.index = ToxicIndex;
			write16(importData, cur, ToxicIndex);
			u32 ind = 0x19001C00 + (BoxIndex * 0x20);
			write32(importData, cur + 4, ind);
			wb.pointer = (u32*)(0x1C00 + (BoxIndex * 0x20));
			BoxIndex++;
			ToxicIndex++;
			cur += 8;
		}
	}
	write16(importData, cur, 0xFFFF); // end list
	cur += 2;
	writeMany(importData, cur, 0xA0 - cur, 0x01); // fill gap with 0x01
	cur += 0xA0 - cur;
	int MistIndex = 0x33;
	for (waterBox& wb : waterBoxes)
	{
		if (wb.type == 2) // mist
		{
			wb.index = MistIndex;
			write16(importData, cur, MistIndex);
			u32 ind = 0x19001C00 + (BoxIndex * 0x20);
			write32(importData, cur + 4, ind);
			wb.pointer = (u32*)(0x1C00 + (BoxIndex * 0x20));
			BoxIndex++;
			MistIndex++;
			cur += 8;
		}
	}
	write16(importData, cur, 0xFFFF); // end list
	cur += 2;
	writeMany(importData, cur, offset - cur, 0x01); // fill gap with 0x01
	cur += offset - cur;

	//size = offset;
	for (waterBox& wb : waterBoxes)
	{
		write32(importData, cur, 0x00010000);
		write32(importData, cur + 4, 0x000F0000 | wb.scale);

		write16(importData, cur + 8, wb.minx);
		write16(importData, cur + 10, wb.minz);
		write16(importData, cur + 12, wb.maxx);
		write16(importData, cur + 14, wb.minz);
		write16(importData, cur + 16, wb.maxx);
		write16(importData, cur + 18, wb.maxz);
		write16(importData, cur + 20, wb.minx);
		write16(importData, cur + 22, wb.maxz);

		if(wb.type == 1)
			write32(importData, cur + 24, 0x000000B4);
		else
			write32(importData, cur + 24, 0x00010078);

		if (wb.type == 1)
			write32(importData, cur + 28, 0x00010000);
		else
			write32(importData, cur + 28, 0x00000000);

		cur += 0x20;
	}

	//printHexBytes(importData, cur);
	if (curImp->curPos + cur <= curImp->limit) {
		memcpy(curImp->data + curImp->curPos, importData, cur);
		curImp->curPos += cur;
	}
	else {
		err.id = 100;
		err.message = "Import data limit has been exceeded!";
	}
}

void importGeoLayout(const char* objPathName, bool forLevel) {
	if (currentPreName != NULL) addObjPtr((char*)currentPreName->c_str());
	objPtrs* curPtr = NULL;
	for (objPtrs op : ptrs) {
		if (op.name.compare(objPathName) == 0) {
			curPtr = &op;
			break;
		}
	}
	if (curPtr != NULL) {
		vector<geoCmd> cmds;
		int geoStart = curSegOffset;
		if (forLevel) {
			cmds.push_back(strGeo("08 00 00 0A 00 A0 00 78 00 A0 00 78"));
			cmds.push_back(strGeo("04 00 00 00"));
			cmds.push_back(strGeo("0C 00 00 00"));
			cmds.push_back(strGeo("04 00 00 00"));
			cmds.push_back(strGeo("09 00 00 64"));
			cmds.push_back(strGeo("04 00 00 00"));
			cmds.push_back(strGeo("19 00 00 00 80 27 63 D4"));
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("0C 01 00 00"));
			cmds.push_back(strGeo("04 00 00 00"));
			cmds.push_back(strGeo("0A 01 00 2D 00 64 7F FF 80 29 AA 3C"));
			cmds.push_back(strGeo("04 00 00 00"));
			cmds.push_back(strGeo("0F 00 00 01 00 00 07 D0 17 70 0C 00 00 00 EE 00 80 28 7D 30"));
			cmds.push_back(strGeo("04 00 00 00"));
			if (waterBoxes.size() > 0) {
				bool hw = 0, ht = 0, hm = 0;
				for (waterBox wb : waterBoxes) {
					if (wb.type == 0) hw = 1;
					else if (wb.type == 1) ht = 1;
					else if (wb.type == 2) hm = 1;
				}
				if (hw) cmds.push_back(strGeo("18 00 50 00 80 2D 10 4C"));
				if (ht) cmds.push_back(strGeo("18 00 50 01 80 2D 10 4C"));
				if (hm) cmds.push_back(strGeo("18 00 50 02 80 2D 10 4C"));
			}
			if (curPtr->solid_ptr) {
				char cmd[24];
				sprintf(cmd, "15 01 00 00 %X %X %X %X",
					(curPtr->solid_ptr >> 24) & 0xFF,
					(curPtr->solid_ptr >> 16) & 0xFF,
					(curPtr->solid_ptr >> 8) & 0xFF,
					curPtr->solid_ptr & 0xFF); // Solid

				cmds.push_back(strGeo(cmd));
			}
			if (curPtr->alpha_ptr) {
				char cmd[24];
				sprintf(cmd, "15 04 00 00 %X %X %X %X",
					curPtr->alpha_ptr >> 24 & 0xFF,
					curPtr->alpha_ptr >> 16 & 0xFF,
					curPtr->alpha_ptr >> 8 & 0xFF,
					curPtr->alpha_ptr & 0xFF); // Alpha
				cmds.push_back(strGeo(cmd));
			}
			if (curPtr->trans_ptr) {
				char cmd[24];
				sprintf(cmd, "15 05 00 00 %X %X %X %X",
					curPtr->trans_ptr >> 24 & 0xFF,
					curPtr->trans_ptr >> 16 & 0xFF,
					curPtr->trans_ptr >> 8 & 0xFF,
					curPtr->trans_ptr & 0xFF); // Trans
				cmds.push_back(strGeo(cmd));
			}
			cmds.push_back(strGeo("17 00 00 00"));
			cmds.push_back(strGeo("18 00 00 00 80 27 61 D0"));
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("0C 00 00 00"));
			cmds.push_back(strGeo("04 00 00 00"));
			cmds.push_back(strGeo("18 00 00 00 80 2C D1 E8"));
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("01 00 00 00"));
		} else {
			cmds.push_back(strGeo("20 00 28 30"));
			cmds.push_back(strGeo("04 00 00 00"));
			if (curPtr->solid_ptr) {
				char cmd[24];
				sprintf(cmd, "15 01 00 00 %X %X %X %X",
					curPtr->solid_ptr >> 24 & 0xFF,
					curPtr->solid_ptr >> 16 & 0xFF,
					curPtr->solid_ptr >> 8 & 0xFF,
					curPtr->solid_ptr & 0xFF); // Solid
				cmds.push_back(strGeo(cmd));
			}
			if (curPtr->alpha_ptr) {
				char cmd[24];
				sprintf(cmd, "15 04 00 00 %X %X %X %X",
					curPtr->alpha_ptr >> 24 & 0xFF,
					curPtr->alpha_ptr >> 16 & 0xFF,
					curPtr->alpha_ptr >> 8 & 0xFF,
					curPtr->alpha_ptr & 0xFF); // Alpha
				cmds.push_back(strGeo(cmd));
			}
			if (curPtr->trans_ptr) {
				char cmd[24];
				sprintf(cmd, "15 05 00 00 %X %X %X %X",
					curPtr->trans_ptr >> 24 & 0xFF,
					curPtr->trans_ptr >> 16 & 0xFF,
					curPtr->trans_ptr >> 8 & 0xFF,
					curPtr->trans_ptr & 0xFF); // Trans
				cmds.push_back(strGeo(cmd));
			}
			cmds.push_back(strGeo("05 00 00 00"));
			cmds.push_back(strGeo("01 00 00 00"));
		}
		for(geoCmd gc: cmds) curSegOffset += gc.len;
		//printf("objPathName(GEO): %s\n", objPathName);
		addPtr((char*)objPathName, PTR_GEO, curSeg << 24 | geoStart);
		if (currentPreName != NULL) addPtr((char*)currentPreName->c_str(), PTR_GEO, curSeg << 24 | geoStart);
		int size = curSegOffset - geoStart;
		u8 * importData = (u8*)malloc(size);
		int segOff = 0;
		for (geoCmd& cmd : cmds) {
			memcpy(importData + segOff, cmd.data, cmd.len);
			segOff += cmd.len;
		}
		//printHexBytes(importData, size);
		if (curImp->curPos + size <= curImp->limit) {
			memcpy(curImp->data + curImp->curPos, importData, size);
			curImp->curPos += size;
		}
		else {
			err.id = 100;
			err.message = "Import data limit has been exceeded!";
		}

		free(importData);
	}
	currentPreName = NULL;
}

void importBin(const char* binPath, int offset, int setSize) {
	if (currentPreName != NULL) {
		addObjPtr((char*)currentPreName->c_str());
		addPtr((char*)currentPreName->c_str(), PTR_START, (curSeg << 24) | curSegOffset);
	}
	FILE* bf = fopen(binPath, "rb");
	int size;
	if (setSize > -1) size = setSize;
	else {
		fseek(bf, offset, SEEK_END);
		size = ftell(bf);
	}
	fseek(bf, offset, SEEK_SET);
	//printf("Number of hex bytes: 0x%X\n", size);

	if (curImp->curPos + size > curImp->limit) {
		err.id = 100;
		err.message = "Import data limit has been exceeded!";
		fclose(bf);
		return;
	}

	fread(curImp->data+curImp->curPos,1,size,bf);
	curImp->curPos += size;
	fclose(bf);
	//printf("Imported %s\n", binPath);
	currentPreName = NULL;
}

void processTweakData(char* data) {
	tweakPatch * curPat = NULL;
	int pos = 0;
	string pch = strtok(data, " ");
	tweakPatch cur;
	cur.address = stoll(pch.substr(0,pch.length()-1), 0, 16);
	while (pch.c_str() != NULL) {
		char* tok = strtok(NULL, " ");
		if (tok == NULL) break;
		pch = tok;
		if (pch.find(":") != string::npos) { // Found address
			tweaks.push_back(cur);
			cur.data.clear();
			cur.address = stoll(pch.substr(0, pch.length() - 1), 0, 16);
		} else {
			cur.data.push_back(stoi(pch,0,16));
		}
	}
	tweaks.push_back(cur);
}

void fixDoubleSpaces(string& str) {
	str.erase(std::unique(str.begin(), str.end(),
		[](char a, char b) { return a == ' ' && b == ' '; }), str.end());
}

void importTweak (string path) {
	ifstream infile;
	infile.open(path);
	stringstream strStream;
	strStream << infile.rdbuf(); // read the file
	string data = strStream.str();
	xml_document<> doc; // character type defaults to char
	doc.parse<0>((char*)data.c_str()); // 0 means default parse flags
	xml_node<> *node = doc.first_node("m64tweak");
	for (xml_node<> *cn = node->first_node(); cn; cn = cn->next_sibling()) {
		//printf("Name of node: %s\n", cn->name());
		if (tolowercase(cn->name()).compare("patch") == 0) {
			string str = cn->value();
			replace(str.begin(), str.end(), '\n', ' '); // Replaces all newlines with a space
			fixDoubleSpaces(str); // Makes sure that there is only a single space between each element
			//printf("%s\n", str);
			processTweakData((char*)str.c_str());
		}
	}
	infile.close();
}

void importHexData(char* hexString) {
	if (currentPreName != NULL) {
		addObjPtr((char*)currentPreName->c_str());
		addPtr((char*)currentPreName->c_str(), PTR_START, (curSeg << 24) | curSegOffset);
	}
	int size = (strlen(hexString)+1)/3;
	u8 * hexData = (u8*)malloc(size);
	if (verbose) printf("Number of hex bytes: %X\n", size);
	if (curImp->curPos + size > curImp->limit) {
		err.id = 100;
		err.message = "Import data limit has been exceeded!";
		return;
	}
	int pos = 0;
	char * pch = strtok(hexString, " ");
	hexData[0] = stoi(pch, 0, 16);
	while (pch != NULL) {
		pch = strtok(NULL, " ");
		if (pch != NULL) {
			hexData[++pos] = stoi(pch, 0, 16);
		}
	}
	
	memcpy(curImp->data+curImp->curPos, hexData, size);
	curImp->curPos += size;
	free(hexData);
	currentPreName = NULL;
}

void setLightAndDarkValues(u32 light, u32 dark) {
	defaultColor[0] = (light >> 24) & 0xFF;
	defaultColor[1] = (light >> 16) & 0xFF;
	defaultColor[2] = (light >> 8) & 0xFF;
	defaultColor[3] = light & 0xFF;
	defaultColor[4] = (light >> 24) & 0xFF;
	defaultColor[5] = (light >> 16) & 0xFF;
	defaultColor[6] = (light >> 8) & 0xFF;
	defaultColor[7] = light & 0xFF;
	defaultColor[8] = (dark >> 24) & 0xFF;
	defaultColor[9] = (dark >> 16) & 0xFF;
	defaultColor[10] = (dark >> 8) & 0xFF;
	defaultColor[11] = dark & 0xFF;
	defaultColor[12] = (dark >> 24) & 0xFF;
	defaultColor[13] = (dark >> 16) & 0xFF;
	defaultColor[14] = (dark >> 8) & 0xFF;
	defaultColor[15] = dark & 0xFF;
}

void parseIntValue(string& input, int& value) {
	if (input.find("0x") == 0)
		value = stoll(input.substr(2), 0, 16);
	else if (input.find("$") == 0)
		value = stoll(input.substr(1), 0, 16);
	else
		value = stoll(input);
}

void parseShortValue(string& input, s16& value) {
	if (input.find("0x") == 0)
		value = stoi(input.substr(2), 0, 16);
	else if (input.find("$") == 0)
		value = stoi(input.substr(1), 0, 16);
	else
		value = stoi(input);
}

void importDataIntoRom() {
	try {
		FILE* ROM = fopen(romPath.c_str(), "rb+");
		for (import& i : importData) {
			//printHexBytes(i.data, i.curPos);
			fseek(ROM, i.address, 0);
			fwrite(i.data, 1, i.curPos, ROM);
			free(i.data);
		}
		importData.clear();
		for (putData put : putImports) {
			fseek(ROM, put.address, 0);
			if (checkLittleEndian()) {
				u32 pd = BYTE_SWAP_32(put.data);
				fwrite(&pd, 4, 1, ROM);
			} else 
				fwrite(&put.data, 4, 1, ROM);
			if (verbose) printf("put 0x%X at ROM address 0x%X\n", put.data, put.address);
		}
		putImports.clear();
		if (tweaks.size() > 0) {
			if (verbose) printf("Importing Tweaks into ROM!\n");
			for (tweakPatch tp : tweaks){
				if (verbose) printf("Rom Address = 0x%X\n", tp.address);
				fseek(ROM, tp.address, 0);
				fwrite((u8*)&tp.data[0], 1, tp.data.size(), ROM);
			}
		}
		tweaks.clear();
		fclose(ROM);
		printf("Import successful!\n");
	} catch (int error) {
		err.id = 101;
		err.message = "Could not import into ROM!";
	}
}

int main(int argc, char *argv[]) {
	string preName;
	vector<vector<string>> arguments;
	for (int i = 1; i < argc;) {
		char * data[256];
		int cnt = 1;
		vector<string> arg;
		arg.push_back(argv[i]);
		for (int j = 1;; ++j) {
			if (i + j >= argc) break;
			if (argv[i + j][0] == '-') 
				if(isalpha(argv[i + j][1])) break; // Won't break with negative numbers.
			data[j] = argv[i + j];
			arg.push_back(argv[i + j]);
			cnt++;
		}
		i += cnt;
		arguments.push_back(arg);
		if (i >= argc) break;
	}

	for (vector<string> arg : arguments) {
		if (err.id) break;
		string cmd = tolowercase(arg[0]);
		if (cmd.compare("-r") == 0 && arg.size() > 1) {
			romPath = arg[1];

			if (!fileExists(romPath)) {
				err.id = 10;
				err.message = "Invalid ROM path";
			}
			if (verbose)
				printf("Importing to ROM file: %s\n", romPath.c_str());
		}
		else if (cmd.compare("-a") == 0) {
			if (arg.size() > 1) {
				parseIntValue(arg[1], (int&)romPos);
				definedSegPtr = false;
			}
			if (arg.size() > 2) {
				if (arg[2].find("0x") == 0) {
					curSeg = stoi(arg[2].substr(2, 2), 0, 16) & 0xFF;
					startSegOffset = stoi(arg[2].substr(4, 6), 0, 16) & 0xFFFFFF;
					curSegOffset = startSegOffset;
				}
				else if (arg[2].find("$") == 0) {
					curSeg = stoi(arg[2].substr(1, 2), 0, 16) & 0xFF;
					startSegOffset = stoi(arg[2].substr(3, 6), 0, 16) & 0xFFFFFF;
					curSegOffset = startSegOffset;
				}
				else {
					err.id = 1;
					err.message = "Segmented pointer not in hex format";
				}
				definedSegPtr = true;
				if (verbose)
					printf("Rom Position: %X, Segment: %X, Offset: %X\n", romPos, curSeg, curSegOffset);
			}

			import i;
			i.address = romPos;
			i.curPos = 0;
			if (setNextLimit != 0) {
				i.limit = setNextLimit;
				setNextLimit = 0;
			}
			else
				i.limit = 0x150000; // Default limit value
			i.data = (u8*)malloc(i.limit);
			importData.push_back(i);
			curImp = &importData.at(importData.size() - 1);
		}
		else if (cmd.compare("-s") == 0 && arg.size() > 1) {
			modelScale = stod(arg[1]);
			if (verbose)
				printf("Model scale size set to: %f\n", modelScale);
		}
		else if (cmd.compare("-o") == 0 && arg.size() > 3) {
			parseShortValue(arg[1], xOff);
			parseShortValue(arg[2], yOff);
			parseShortValue(arg[3], zOff);
		}
		else if (cmd.compare("-option") == 0 && arg.size() > 1) {
			arg[1] = tolowercase(arg[1]);
			if (arg[1].compare("rvl") == 0 && arg.size() > 2) {
				reduceVertLevel = stoi(arg[2]);
			}
		}
		else if (cmd.compare("-sv") == 0 && arg.size() > 2) {
			u32 light, dark;
			parseIntValue(arg[1], (int&)light);
			parseIntValue(arg[2], (int&)dark);
			setLightAndDarkValues(light, dark);
		}
		else if (cmd.compare("-c") == 0 && arg.size() > 1) {
			if (curImp != NULL) {
				parseIntValue(arg[1], (int&)curImp->limit);
				curImp->data = (u8*)realloc((u8*)curImp->data, curImp->limit);
				if (verbose)
					printf("Data limit set to: 0x%X\n", curImp->limit);
			}
			else {
				parseIntValue(arg[1], (int&)setNextLimit);
				if (verbose)
					printf("Data limit set to: 0x%X\n", setNextLimit);
			}
		}
		else if (cmd.compare("-io") == 0 && arg.size() > 1) {
			if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (definedSegPtr) {
				if (arg.size() == 2) {
					importOBJ(arg[1].c_str(), NULL);
				}
				else if (arg.size() > 2) {
					if (arg.size() > 3) currentPreName = &arg[3];
					if (tolowercase(arg[2]).compare("null") == 0)
						importOBJ(arg[1].c_str(), NULL);
					else
						importOBJ(arg[1].c_str(), arg[2].c_str());
				}
			}
			else {
				err.id = 4;
				err.message = "You did not define a segmented pointer!\n";
			}
		}
		else if (cmd.compare("-ioc") == 0) {
			if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (arg.size() > 2) {
				if (arg.size() > 3) currentPreName = &arg[3];
				importOBJCollision(arg[1].c_str(), arg[2].c_str(), false);
			}
		}
		else if (cmd.compare("-ig") == 0) {
			if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (arg.size() > 2) {
				if (arg.size() > 3) currentPreName = &arg[3];
				if (verbose)
					printf("Adding geo layout! Args: %s, %s\n", arg[1].c_str(), arg[2].c_str());
				importGeoLayout(arg[1].c_str(), stoi(arg[2].c_str()) > 0 ? true : false);
			}
		}
		else if (cmd.compare("-ih") == 0) {
			if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (arg.size() > 1) {
				if (arg.size() > 2) currentPreName = &arg[2];
				importHexData((char*)trim(arg[1]).c_str());
			}
		}
		else if (cmd.compare("-it") == 0) {
			if (arg.size() > 1)
				importTweak(arg[1]);
		}
		else if (cmd.compare("-ib") == 0) {
			if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (arg.size() > 1) {
				int off = 0;
				int size = -1;
				if (arg.size() > 3) {
					parseIntValue(arg[2], off);
					parseIntValue(arg[3], size);
					if (arg.size() > 4) currentPreName = &arg[4];
				}
				importBin(arg[1].c_str(), off, size);
			}
		}
		else if (cmd.compare("-sgm") == 0 && arg.size() > 1) {
			geoModeData = arg[1];
		}
		else if (cmd.compare("-ct") == 0 && arg.size() > 1) {
			colorTexData = arg[1];
		}
		else if (cmd.compare("-tt") == 0 && arg.size() > 1) {
			texTypeData = arg[1];
		}
		else if (cmd.compare("-df") == 0 && arg.size() > 6) {
			arg[1] = tolowercase(arg[1]);
			if (arg[1].compare("t") == 0 || arg[1].compare("1") == 0 || arg[1].compare("true") == 0)
				enableDeathFloor = true;
			else if (arg[1].compare("f") == 0 || arg[1].compare("0") == 0 || arg[1].compare("false") == 0)
				enableDeathFloor = false;
			parseShortValue(arg[2], dfx1);
			parseShortValue(arg[3], dfz1);
			parseShortValue(arg[4], dfx2);
			parseShortValue(arg[5], dfz2);
			parseShortValue(arg[6], dfy);
			if (verbose)
				printf("Death floor %d %d %d %d %d %d\n", enableDeathFloor, dfx1, dfz1, dfx2, dfz2, dfy);
		}
		else if (cmd.compare("-wbc") == 0) {
			waterBoxes.clear();
		}
		else if (cmd.compare("-wb") == 0 && arg.size() > 8) {
			//BOX_TYPE INDEX X1_POS Z1_POS X2_POS Z2_POS HEIGHT SCALE
			waterBox wb;
			parseShortValue(arg[1], (s16&)wb.type);
			parseShortValue(arg[2], (s16&)wb.index);

			s16 minx, minz, maxx, maxz;
			parseShortValue(arg[3], minx);
			parseShortValue(arg[4], minz);
			parseShortValue(arg[5], maxx);
			parseShortValue(arg[6], maxz);
			parseShortValue(arg[7], wb.y);
			parseShortValue(arg[8], (s16&)wb.scale);
			wb.pointer = 0;
			if (minx > maxx) {
				int t = minx;
				minx = maxx;
				maxx = t;
			}
			if (minz > maxz) {
				int t = minz;
				minz = maxz;
				maxz = t;
			}
			wb.minx = minx;
			wb.minz = minz;
			wb.maxx = maxx;
			wb.maxz = maxz;
			printf("Adding water box: %d %d %d %d\n", minx, minz, maxx, maxz);
			waterBoxes.push_back(wb);
		}
		else if (cmd.compare("-wd") == 0) {
			importWaterBoxData(0x400);
		}
		else if (cmd.compare("-n") == 0 && arg.size() > 1) {
			preName = arg[1];
			currentPreName = &preName;
		}
		else if (cmd.compare("-vft") == 0 && arg.size() > 1) {
			arg[1] = tolowercase(arg[1]);
			if (arg[1].compare("t") == 0 || arg[1].compare("1") == 0 || arg[1].compare("true") == 0)
				flipTexturesVertically = true;
			else if (arg[1].compare("f") == 0 || arg[1].compare("0") == 0 || arg[1].compare("false") == 0)
				flipTexturesVertically = false;
			if(verbose)
				printf("Am I flipping the textures vertically? %s\n", flipTexturesVertically ? "Yes I am!" : "No I'm not");
		}
		else if (cmd.compare("-v") == 0) {
			verbose = true;
		}
		else if (cmd.compare("-center") == 0 && arg.size() > 1) {
			arg[1] = tolowercase(arg[1]);
			if (arg[1].compare("t") == 0 || arg[1].compare("1") == 0 || arg[1].compare("true") == 0)
				centerVerts = true;
			else if (arg[1].compare("f") == 0 || arg[1].compare("0") == 0 || arg[1].compare("false") == 0)
				centerVerts = false;
		}
		else if (cmd.compare("-put") == 0 && arg.size() > 3) {
			arg[2] = tolowercase(arg[2]);
			putData put;
			for (objPtrs op : ptrs) {
				if (op.name.compare(arg[1].c_str()) == 0) {
					if (arg[2].compare("start") == 0) 
						put.data = op.start_ptr;
					else if (arg[2].compare("solid") == 0)
						put.data = op.solid_ptr;
					else if (arg[2].compare("alpha") == 0)
						put.data = op.alpha_ptr;
					else if (arg[2].compare("trans") == 0 || arg[2].compare("transparency") == 0)
						put.data = op.trans_ptr;
					else if (arg[2].compare("geo") == 0)
						put.data = op.geo_ptr;
					else if (arg[2].compare("col") == 0 || arg[2].compare("collision") == 0)
						put.data = op.col_ptr;
				}
			}
			parseIntValue(arg[3], (int&)put.address);

			if (arg.size() > 4) { // Add offset
				int add = 0;
				parseIntValue(arg[4], add);
				put.address += add;
			}

			putImports.push_back(put);
		}
		else if (cmd.compare("-fog") == 0 && arg.size() > 1) {
			//-fog ENABLED {FOG_TYPE RED_VALUE GREEN_VALUE BLUE_VALUE}
			arg[1] = tolowercase(arg[1]);
			if (arg[1].compare("t") == 0 || arg[1].compare("1") == 0 || arg[1].compare("true") == 0)
				enableFog = true;
			if (arg[1].compare("f") == 0 || arg[1].compare("0") == 0 || arg[1].compare("false") == 0)
				enableFog = false;

			// Fog type
			if (arg.size() > 2) {
				arg[2] = tolowercase(arg[2]);
				if (arg[2].compare("0") == 0 || arg[2].compare("subtle 1") == 0 || arg[2].compare("subtle1") == 0)
					fogType = FOG_SUBTLE_1;
				else if (arg[2].compare("1") == 0 || arg[2].compare("subtle 2") == 0 || arg[2].compare("subtle2") == 0)
					fogType = FOG_SUBTLE_2;
				else if (arg[2].compare("2") == 0 || arg[2].compare("moderate 1") == 0 || arg[2].compare("moderate1") == 0)
					fogType = FOG_MODERATE_1;
				else if (arg[2].compare("3") == 0 || arg[2].compare("moderate 2") == 0 || arg[2].compare("moderate2") == 0)
					fogType = FOG_MODERATE_2;
				else if (arg[2].compare("4") == 0 || arg[2].compare("moderate 3") == 0 || arg[2].compare("moderate3") == 0)
					fogType = FOG_MODERATE_3;
				else if (arg[2].compare("5") == 0 || arg[2].compare("moderate 4") == 0 || arg[2].compare("moderate4") == 0)
					fogType = FOG_MODERATE_4;
				else if (arg[2].compare("6") == 0 || arg[2].compare("intense") == 0)
					fogType = FOG_INTENSE;
				else if (arg[2].compare("7") == 0 || arg[2].compare("very intense") == 0 || arg[2].compare("veryintense") == 0)
					fogType = FOG_VERY_INTENSE;
				else if (arg[2].compare("8") == 0 || arg[2].compare("hardcore") == 0)
					fogType = FOG_HARDCORE;
			}

			if (arg.size() > 5) {
				parseIntValue(arg[3], (int&)fogRed);
				parseIntValue(arg[4], (int&)fogGreen);
				parseIntValue(arg[5], (int&)fogBlue);
			}
		}

		else if (cmd.compare("-run") == 0 && arg.size() > 1) {
			printf("Running external program: %s\n", arg[1].c_str());
			if (system(arg[1].c_str()) != 0) {
				err.id = 10000;
				err.message = "External program error!";
			}
		}

	}

	if (romPath.empty()) {
		err.id = 2;
		err.message = "You have not defined a path to the ROM!";
	}

	if (err.id)
		printf("Error %d: %s\n", err.id, err.message.c_str());
	else
		importDataIntoRom();
	
	waterBoxes.clear();
	resetVariables();
	return err.id;
}
