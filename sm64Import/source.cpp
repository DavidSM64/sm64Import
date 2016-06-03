/*
sm64Import v0.0 by Davideesk

TODO list:
* Support IA texture types
* Support other image types other than just .png

*/

#include "lodepng.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <stdio.h>
#include <intrin.h>
using namespace std;
using namespace lodepng;

typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

enum MaterialType
{
	NOT_DEFINED,
	TEXTURE_SOLID,
	TEXTURE_ALPHA,
	COLOR_SOLID
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
	bool isTextureCopy;
	u32 color; // RGBA
	u32 offset;
	u32 size;
	u32 texWidth;
	u32 texHeight;
	MaterialType type;
	u16 collision;
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
	u8 data[8];
} f3d;

typedef struct {
	int position;
	int cLen;
	material * mat;
} materialPosition;

typedef struct {
	u32 id;
	u32 offset;
	u32 width;
	u32 height;
	vector<u16> data;
} textureEntry;


vector<vertex> verts;
vector<normal> norms;
vector<texCord> uvs;
vector<material> materials;
vector<materialPosition> matPos;
vector<face> faces;
vector<finalVertex> combinedVerts;
vector<textureEntry> textureBank;

material* currentMaterial;
int currentFace = 0;
int lastPos = 0;
int lastOffset = 0;
bool createAlphaDL = false;
bool definedSegPtr = false;
MaterialType lastTextureType = NOT_DEFINED;

/* Tool Variables */
string romPath;
string collisionString;
u32 dataLimit = 0x150000;
u32 romPos = 0;
u8 curSeg = 0;
u32 startSegOffset = 0;
u32 curSegOffset = 0;
bool flipTexturesVertically = false;
float modelScale = 1.0f;
s16 xOff = 0;
s16 yOff = 0;
s16 zOff = 0;
bool enableFog = false;
FogType fogType = FOG_VERY_INTENSE;
u8 fogRed = 0x00, fogGreen = 0x7F, fogBlue = 0xFF;

void importOBJCollision(const char*, const char*, bool);

vector<string> splitString(string str, string split) {
	vector<string> result;
	char * pch = strtok((char*)str.c_str(), (char*)split.c_str());
	while (pch != NULL) {
		result.push_back(pch);
		pch = strtok(NULL, (char*)split.c_str());
	}
	return result;
}

bool checkForTextureCopy(material * mat, vector<u16>& cur) {
	if (textureBank.size() == 0) return false;
	for (textureEntry& entry : textureBank) {
		if (entry.data.size() != cur.size()) continue;
		bool pass = false;
		for (int i = 0; i < entry.data.size(); ++i) {
			if (entry.data[i] != cur[i]) { 
				pass = false;
				break; 
			}
			pass = true;
		}
		if (pass) {
			//printf("Duplicate Texture found!\n");
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

int processRGBAImage(const char* filename, material * mat) {
	vector<u8> data;
	u32 width, height;

	u32 error = decode(data, width, height, filename);
	if (error) {
		cout << "decoder error " << error << ": " << lodepng_error_text(error) << endl;
		return 0;
	}
	//printf("Texture '%s': w=%d, h=%d\n",filename,width,height);
	mat->texWidth = width;
	mat->texHeight = height;
	vector<u16> tex = vector<u16>(data.size() / 4);

	mat->type = TEXTURE_SOLID;

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			u8 r = (data[(y*width + x) * 4 + 0] / 8) & 31;
			u8 g = (data[(y*width + x) * 4 + 1] / 8) & 31;
			u8 b = (data[(y*width + x) * 4 + 2] / 8) & 31;
			u8 a = (data[(y*width + x) * 4 + 3] > 0) ? 1 : 0;

			if (a == 0) {
				mat->hasTextureAlpha = true;
				createAlphaDL = true;
				mat->type = TEXTURE_ALPHA;
			}

			if (flipTexturesVertically)
				tex[(height - 1 - y)*width + x] = _byteswap_ushort((r << 11) | (g << 6) | (b << 1) | a);
			else
				tex[y*width + x] = _byteswap_ushort((r << 11) | (g << 6) | (b << 1) | a);
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
	}

	mat->texture = textureBank.size()-1;
	mat->hasTexture = true;
	return data.size() / 2;
}

void processMaterialColor(string str, material * mat) {
	vector<string> splitColor = splitString(str, " ");
	int r = stod(splitColor[0]) * 255;
	int g = stod(splitColor[1]) * 255;
	int b = stod(splitColor[2]) * 255;
	mat->color = r << 24 | g << 16 | b << 8 | 0xFF;
}

void processMaterialLib(string str) {
	string line;
	ifstream infile;
	infile.open(str);
	int size = 0;
	while (!infile.eof()) {
		getline(infile, line);
		//cout << line << endl;
		if (line.find("newmtl ") == 0) {
			material m;
			m.type = COLOR_SOLID;
			m.color = 0;
			m.hasTexture = false;
			m.hasTextureAlpha = false;
			m.isTextureCopy = false;
			m.name = line.substr(7);
			m.collision = 0;
			materials.push_back(m);
			if (materials.size() > 1) {
				if (size == 0) size = 0x10;
				if (!materials.at(materials.size() - 2).isTextureCopy) {
					(&materials.at(materials.size() - 2))->offset = lastOffset;
					(&materials.at(materials.size() - 2))->size = size;
					lastOffset += size;
				}
			}
			size = 0x10;
		} else if (line.find("Kd ") == 0) {
			processMaterialColor(line.substr(3).c_str(), &materials.at(materials.size() - 1));
		} else if (line.find("map_Kd ") == 0) {
			//cout << "Found image: " << line << endl;
			size = processRGBAImage(line.substr(7).c_str(), &materials.at(materials.size()-1));
		}
	}
	if (!materials.at(materials.size() - 1).isTextureCopy) {
		(&materials.at(materials.size() - 1))->offset = lastOffset;
		(&materials.at(materials.size() - 1))->size = size;
	}
	infile.close();
}

void processVertex(string str) {
	vector<string> splitXYZ = splitString(str, " ");
	vertex v;
	v.x = (stof(splitXYZ[0]) * modelScale) + xOff;
	v.y = (stof(splitXYZ[1]) * modelScale) + yOff;
	v.z = (stof(splitXYZ[2]) * modelScale) + zOff;
	//printf("Vertex: %04X,%04X,%04X\n", v.x, v.y, v.z);
	verts.push_back(v);
}

void processUV(string str) {
	string u = str.substr(0, str.find(' '));
	string v = str.substr(u.length() + 1);
	texCord uv;
	//printf("UV: %04X, %04X\n", uv.u, uv.v);
	//printf("%s\n", str.c_str());
	uv.u = stof(u) * 32 * 32;
	uv.v = -(stof(v) * 32 * 32);
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
	//cout << "Normal: " << hex << (s16)n.a << "//" << (s16)n.b << "//" << (s16)n.c << endl;
}

// Debug thing
void printHexBytes(u8 * arr, int size){
	for (int i = 0; i < size; i++) printf("%02X ", arr[i]);
	printf("\n");
}
// Debug thing
void printHexShorts(u16 * arr, int size) {
	for (int i = 0; i < size; i++) printf("%04X ", arr[i]);
	printf("\n");
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
		ta.u = ta.u * (f.mat->texWidth / 32) - 0x10;
		ta.v = ta.v * (f.mat->texHeight / 32) - 0x10;
		tb.u = tb.u * (f.mat->texWidth / 32) - 0x10;
		tb.v = tb.v * (f.mat->texHeight / 32) - 0x10;
		tc.u = tc.u * (f.mat->texWidth / 32) - 0x10;
		tc.v = tc.v * (f.mat->texHeight / 32) - 0x10;

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

void addMaterialPosition(string str) {
	for (auto &m : materials) {
		//printf("comparing %s vs %s\n", m.name.c_str(), str.c_str());
		if (strcmp(m.name.c_str(), str.c_str()) == 0) {
			//printf("Adding positions! %d & %d\n", lastPos, currentFace);
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
		//cout << "Found material lib: " << str << endl;
		processMaterialLib(str.substr(7));
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
	if(alpha)
		cmds.push_back(strF3D("B9 00 03 1D C8 11 30 78"));
	else
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
		u32 off = startSegOffset + addOffset + mat.offset + 0x10;
		char cmd[24];
		sprintf(cmd, "03 86 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		off = startSegOffset + addOffset + mat.offset + 0x18;
		sprintf(cmd, "03 88 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
	} else {
		u32 off = startSegOffset;
		char cmd[24];
		sprintf(cmd, "03 86 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		off = startSegOffset + 8;
		sprintf(cmd, "03 88 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
	}
}
/*
04 AB CCCC DD EEEEEE
A = number of vertices - 1
B = vertex index in buffer to start copying into
C = length (in bytes = (A+1)*0x10)
D = segment to copy from
E = offset in segment to copy from
*/
void addCmd04(vector<f3d>& cmds, material& mat, int left, int offset) {
	u32 off = startSegOffset+offset;
	int amount = 0xF0;
	if (left < 5) amount = (left * 3)*0x10;
	char cmd[24];
	sprintf(cmd, "04 %X 00 %X %X %X %X %X", amount-0x10, amount, curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
	cmds.push_back(strF3D(cmd));
}

void addCmdBF(vector<f3d>& cmds, material& mat, int l) {
	char cmd[24];
	sprintf(cmd, "BF 00 00 00 00 %X %X %X", (l * 3) * 0xA, (l * 3 + 1) * 0xA, (l * 3 + 2) * 0xA);
	cmds.push_back(strF3D(cmd));
}

/*
F2 00 00 00 00 WW WH HH
Where WWW = (width - 1) << 2 and HHH = (height - 1) << 2
*/
void addCmdF2(vector<f3d>& cmds, material& mat) {
	u16 width = ((mat.texWidth - 1) << 2) & 0xFFF;
	u16 height = ((mat.texHeight - 1) << 2) & 0xFFF;
	u32 data = (width << 12) | height;
	char cmd[24];
	sprintf(cmd, "F2 00 00 00 00 %X %X %X", (data >> 16) & 0xFF, (data >> 8) & 0xFF, data & 0xFF);
	cmds.push_back(strF3D(cmd));
}

/*
F3 00 00 00 07 7F F1 00 for 32x64 or 64x32 RGBA Textures
F3 00 00 00 07 3F F1 00 for 32x32 RGBA Textures
*/
void addCmdF3(vector<f3d>& cmds, material& mat) {
	if (mat.hasTexture) {
		if (mat.texWidth == 32 && mat.texHeight == 32) 
			cmds.push_back(strF3D("F3 00 00 00 07 3F F1 00"));
		else
			if(mat.texWidth == 64)
				cmds.push_back(strF3D("F3 00 00 00 07 7F F0 80"));
			else
				cmds.push_back(strF3D("F3 00 00 00 07 7F F1 00"));
	}
}

/*
F5 10 00 00 07 00 00 00 : Always loaded first for normal RGBA, followed by another F5 command
F5 10 10 00 00 01 40 50 : Standard usage for 32x32 textures after G_SETTILESIZE
F5 10 10 00 00 01 80 50 : Standard usage for 32x64 textures after G_SETTILESIZE
F5 10 20 00 00 01 40 60 : Standard usage for 64x32 textures after G_SETTILESIZE
*/
void addCmdF5(vector<f3d>& cmds, material& mat, bool first) {
	if (first) {
		if(mat.type == TEXTURE_SOLID || mat.type == TEXTURE_ALPHA)
			cmds.push_back(strF3D("F5 10 00 00 07 00 00 00"));
	} else {
		if (mat.type == TEXTURE_SOLID || mat.type == TEXTURE_ALPHA) {
			if (mat.texWidth == 32 && mat.texHeight == 32) 
				cmds.push_back(strF3D("F5 10 10 00 00 01 40 50"));
			else 
				if(mat.texHeight == 64)
					cmds.push_back(strF3D("F5 10 10 00 00 01 80 50"));
				else
					cmds.push_back(strF3D("F5 10 20 00 00 01 40 60"));
		}
	}
}

/*
FC 12 7F FF FF FF F8 38 - Standard usage for solid RGBA textures
FC 12 18 24 FF 33 FF FF - Standard usage for alpha RGBA textures
FC FF FF FF FF FE 7B 3D - Standard usage for solid RGBA color
*/
void addCmdFC(vector<f3d>& cmds, material& mat) {
	if (mat.hasTexture) {
		if (mat.hasTextureAlpha)
			if(enableFog)
				cmds.push_back(strF3D("FC FF FF FF FF FC F2 38"));
			else
				cmds.push_back(strF3D("FC 12 18 24 FF 33 FF FF"));
		else
			cmds.push_back(strF3D("FC 12 7F FF FF FF F8 38"));
	}
	else
		cmds.push_back(strF3D("FC FF FF FF FF FE 7B 3D"));
}

void addCmdFD(vector<f3d>& cmds, material& mat) {
	if (mat.hasTexture) {
		u32 off = startSegOffset + mat.offset + 0x10;
		char cmd[24];
		sprintf(cmd, "FD 10 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
	}
}

void importOBJ(const char* objPath, const char* col_data) {
	printf("Importing '%s'\n", objPath);

	string line;
	ifstream infile;
	infile.open(objPath);
	while (!infile.eof()) {
		getline(infile, line);
		processLine(line);
	}
	processFaces();
	infile.close();
	//printf("Import Size: 0x%x\n", 0x10+getCombinedTextureSizes()+getCombinedVertSizes());

	if (curSegOffset % 16 != 0) curSegOffset += 16 - (curSegOffset % 16); // DEBUG (Add padding)

	int importStart = romPos + curSegOffset;
	printf("importStart: 0x%X\n", importStart);
	curSegOffset += 0x10;
	for (material mt : materials) {
		//printf("material '%s' offset = 0x%x, size = 0x%x\n", mt.name.c_str(), mt.offset, mt.size);
		if (!mt.isTextureCopy)curSegOffset += mt.size;
	}

	int startVerts = curSegOffset;
	for (finalVertex fv : combinedVerts) {
		//printHexBytes(fv.data, 16); // Vertices
		curSegOffset += 0x10;
	}
	//printf("Number of tri: %X\n",((curSegOffset-startVerts)/0x10)/3);
	// We need to seperate the model into at least 2 seperate DLs.
	vector<f3d> solidCmds; // For textures with no alpha bits in them. (Includes solid RGBA colors)
	vector<f3d> alphaCmds; // For textures that have atleast 1 alpha bit in them.

	printf("F3D solid starts at 0x%X\n", curSegOffset);
	solidCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("B7 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("BB 00 00 01 FF FF FF FF"));
	solidCmds.push_back(strF3D("E8 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
	if (enableFog) addFogStart(solidCmds, false);
	for (int i = 0; i < matPos.size(); ++i) {
		materialPosition mp = matPos.at(i);
		if (mp.mat->hasTextureAlpha) continue;
		//printf("Current type: %d\n", mp.mat->type);
		if (lastTextureType != mp.mat->type) {
			addCmdFC(solidCmds, *mp.mat);
			addCmd03(solidCmds, *mp.mat, importStart - romPos);
			lastTextureType = mp.mat->type;
		}
		else {
			if (lastTextureType == COLOR_SOLID && mp.mat->type == COLOR_SOLID) {
				addCmd03(solidCmds, *mp.mat, importStart-romPos);
			}
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
		int length;
		//printf("Material '%s': %d \n", mp.mat->name.c_str(), mp.position);
		if (i < matPos.size() - 1) length = matPos.at(i + 1).position - mp.position;
		else length = currentFace - mp.position;
		//printf("Length: %d \n", length);
		for (int l = 0; l < length; l++) {
			if (l % 5 == 0) {
				addCmd04(solidCmds, *mp.mat, length - l, startVerts + ((mp.position * 3 + l * 3) * 0x10));
			}
			addCmdBF(solidCmds, *mp.mat, l % 5);
		}

	}
	if (enableFog) addFogEnd(solidCmds);
	solidCmds.push_back(strF3D("BB 00 00 00 FF FF FF FF"));
	solidCmds.push_back(strF3D("B8 00 00 00 00 00 00 00"));
	//for (f3d cmd : solidCmds) printHexBytes(cmd.data, 8);
	curSegOffset += solidCmds.size() * 8;

	if (createAlphaDL) {
		printf("F3D alpha starts at 0x%X\n", curSegOffset);
		alphaCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
		if (enableFog) alphaCmds.push_back(strF3D("B9 00 02 01 00 00 00 00"));
		alphaCmds.push_back(strF3D("B7 00 00 00 00 00 00 00"));
		alphaCmds.push_back(strF3D("BB 00 00 01 FF FF FF FF"));
		alphaCmds.push_back(strF3D("E8 00 00 00 00 00 00 00"));
		alphaCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
		if (enableFog) addFogStart(alphaCmds, true);
		for (int i = 0; i < matPos.size(); ++i) {
			materialPosition mp = matPos.at(i);
			if (!mp.mat->hasTextureAlpha) continue;
			//printf("Current type: %d\n", mp.mat->type);
			if (lastTextureType != mp.mat->type) {
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
			int length;
			//printf("Material '%s': %d \n", mp.mat->name.c_str(), mp.position);
			if (i < matPos.size() - 1) length = matPos.at(i + 1).position - mp.position;
			else length = currentFace - mp.position;
			//printf("Length: %d \n", length);
			for (int l = 0; l < length; l++) {
				if (l % 5 == 0) {
					addCmd04(alphaCmds, *mp.mat, length - l, startVerts + ((mp.position * 3 + l * 3) * 0x10));
				}
				addCmdBF(alphaCmds, *mp.mat, l % 5);
			}
		}
		if (enableFog) addFogEnd(alphaCmds);
		alphaCmds.push_back(strF3D("FC FF FF FF FF FE 79 3C"));
		alphaCmds.push_back(strF3D("BB 00 00 00 FF FF FF FF"));
		alphaCmds.push_back(strF3D("B8 00 00 00 00 00 00 00"));
		curSegOffset += alphaCmds.size() * 8;
		//for (f3d cmd : alphaCmds) printHexBytes(cmd.data, 8);
	}

	int size = curSegOffset - (importStart-romPos);
	//printf("Total Size = 0x%X\n", size);

	u8 * importData = (u8*)malloc(size);
	u8 defaultColor[16] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x7F,0x7F,0xFF,0x7F,0x7F,0x7F,0xFF };
	memcpy(importData, defaultColor, 0x10);
	int curOff = 0x10;
	for (material mt : materials) {
		//printf("material '%s' offset = 0x%x, size = 0x%x\n", mt.name.c_str(), mt.offset, mt.size);
		if (mt.hasTexture) {
			if (!mt.isTextureCopy) {
				memcpy(importData + curOff, (u8*)&textureBank.at(mt.texture).data[0], mt.size);
				curOff += mt.size;
			}
		}
		else {
			u8 colorData[16];
			u8 r = (mt.color >> 24) & 0xFF;
			u8 g = (mt.color >> 16) & 0xFF;
			u8 b = (mt.color >> 8) & 0xFF;
			u8 a = mt.color & 0xFF;
			//printf("Color: r=0x%X g=0x%X b=0x%X a=0x%X\n",r,g,b,a);
			colorData[0] = r;
			colorData[1] = g;
			colorData[2] = b;
			colorData[3] = a;
			colorData[4] = r;
			colorData[5] = g;
			colorData[6] = b;
			colorData[7] = a;
			colorData[8] = r*0.8;
			colorData[9] = g*0.8;
			colorData[10] = b*0.8;
			colorData[11] = a;
			colorData[12] = r*0.8;
			colorData[13] = g*0.8;
			colorData[14] = b*0.8;
			colorData[15] = a;
			//printHexBytes(colorData, 0x10);
			//printf("Color located at offset %X\n", curOff);
			memcpy(importData + curOff, colorData, 0x10);
			curOff += 0x10;
		}
	}
	for (finalVertex fv : combinedVerts) {
		memcpy(importData + curOff, fv.data, 0x10);
		curOff += 0x10;
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

	FILE* ROM = fopen(romPath.c_str(), "rb+");
	fseek(ROM, importStart, 0);
	fwrite(importData, 1, size, ROM);
	fclose(ROM);
	free(importData);
	if(col_data != NULL) importOBJCollision(objPath, col_data, true);
	resetVariables();
}

void importOBJCollision(const char* objPath, const char* col_data, bool continued) {
	if (!continued) {
		resetVariables();
		string line;
		ifstream infile;
		infile.open(objPath);
		while (!infile.eof()) {
			getline(infile, line);
			processLine(line);
		}
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
		//printf("Material '%s': %d \n", mp->mat->name.c_str(), mp->position);
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
		//printf("Length: %d \n", length);
	}

	int collisionStart = curSegOffset;
	vector<u16> collision;
	collision.push_back(0x0040);
	collision.push_back(verts.size());
	curSegOffset += 4;

	for (vertex v : verts) {
		collision.push_back(v.x);
		collision.push_back(v.y);
		collision.push_back(v.z);
		curSegOffset += 6;
	}

	for (int i = 0; i < matPos.size(); ++i) {
		materialPosition * mp = &matPos.at(i);
		if (mp->position < 0 || mp->cLen < 1) continue;
		curSegOffset += 4;
		//printf("mat '%s': pos = %d, len = %d, collision = %d\n", mp->mat->name.c_str(), mp->position, mp->cLen, mp->mat->collision);
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
	collision.push_back(0x0041);
	collision.push_back(0x0042);
	curSegOffset += 4;
	printf("Collision starts at 0x%X\n", collisionStart);
	//printHexShorts((u16*)&collision[0], collision.size());

	for (u16& col : collision) col = _byteswap_ushort(col);
	//printf("Collision length = %X\n",collision.size()*2);

	FILE* ROM = fopen(romPath.c_str(), "rb+");
	fseek(ROM, romPos + collisionStart, 0);
	fwrite((u8*)&collision[0], 1, collision.size() * 2, ROM);
	fclose(ROM);
}

int main(int argc, char *argv[]) {

	vector<vector<string>> arguments;
	for (int i = 1; i < argc;) {
		char * data[10];
		int cnt = 1;
		vector<string> arg;
		arg.push_back(argv[i]);
		for (int j = 1; j < 10; ++j) {
			if (i + j >= argc) break;
			if (argv[i + j][0] == '-') break;
			data[j] = argv[i + j];
			arg.push_back(argv[i+j]);
			cnt++;
		}
		i += cnt;
		arguments.push_back(arg);
		if (i >= argc) break;
	}

	for (vector<string> arg : arguments) {
		string cmd = arg[0];
		if (cmd.compare("-r") == 0 && arg.size() > 1) {
			romPath = arg[1];
			//printf("Importing to ROM file: %s\n", romPath.c_str());
		} 
		else if (cmd.compare("-a") == 0) {
			if (arg.size() > 1) { 
				if (arg[1].find("0x") == 0)
					romPos = stoi(arg[1].substr(2), 0, 16);
				else if (arg[1].find("$") == 0)
					romPos = stoi(arg[1].substr(1), 0, 16);
				else
					romPos = stoi(arg[1]);
				definedSegPtr = false;
			}
			if (arg.size() > 2) {
				if (arg[2].find("0x") == 0) {
					curSeg = stoi(arg[2].substr(2, 2), 0, 16) & 0xFF;
					startSegOffset = stoi(arg[2].substr(4, 6), 0, 16) & 0xFFFFFF;
					curSegOffset = startSegOffset;
					//printf("Segment: %X Offset: %X\n", curSeg, startSegOffset);
				} else if (arg[2].find("$") == 0) {
					curSeg = stoi(arg[2].substr(1, 2), 0, 16) & 0xFF;
					startSegOffset = stoi(arg[2].substr(3, 6), 0, 16) & 0xFFFFFF;
					curSegOffset = startSegOffset;
					//printf("Segment: %X Offset: %X\n", curSeg, startSegOffset);
				} else {
					printf("Error: Segmented pointer not in hex format\n");
				}
				definedSegPtr = true;
				printf("Rom Position: %X, Segment: %X, Offset: %X\n", romPos, curSeg, curSegOffset);
			}
		}
		else if (cmd.compare("-s") == 0 && arg.size() > 1) {
			modelScale = stof(arg[1]);
			//printf("Model scale size set to: %f\n", modelScale);
		}
		else if (cmd.compare("-c") == 0 && arg.size() > 1) {
			if (arg[1].find("0x") == 0)
				dataLimit = stoi(arg[1].substr(2), 0, 16);
			else if (arg[1].find("$") == 0)
				dataLimit = stoi(arg[1].substr(1), 0, 16);
			else
				dataLimit = stoi(arg[1]);
			//printf("Data limit set to: 0x%X\n", dataLimit);
		}
		else if (cmd.compare("-io") == 0 && arg.size() > 1) {
			if (definedSegPtr) {
				if (arg.size() == 2) {
					importOBJ(arg[1].c_str(), NULL);
				} else if (arg.size() > 2) {
					importOBJ(arg[1].c_str(), arg[2].c_str());
				}
			} else {
				printf("Error: You did not define a segmented pointer!\n");
			}
		}
	}

	//romPath = "C:/Users/David/Desktop/sm64IMPORTTEST.z64";
	//importOBJ("Test_Area1.obj", "Warp_Area2:0x1C");
	//importOBJ("Test_Area2.obj", "Warp_Area1:0x1B,Warp_Area3:0x1D,FireRock:0x01,FireRock_Orange:0x01");
	//importOBJ("Test_Area3.obj", "Warp_Area2:0x1C,Warp_Area4:0x1E");
	//importOBJ("Test_Area4.obj", "Warp_Area3:0x1D");

	printf("Import successful!\n");
	return 0;
}