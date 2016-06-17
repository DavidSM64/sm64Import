/*
sm64Import v0.0 by Davideesk

TODO list for next version:
* Support other texture types besides RGBA16
* Support other image types other than just .png
*/

#include "lodepng.h"
#include "rapidxml.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <sstream>
#include <stdio.h>
#include <intrin.h>
using namespace std;
using namespace lodepng;
using namespace rapidxml;

typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

#define SGM_CULL_FRONT 0x1000;
#define SGM_CULL_BACK 0x2000;
#define SGM_CULL_BOTH 0x3000;

enum MaterialType
{
	NOT_DEFINED,
	TEXTURE_SOLID,
	TEXTURE_ALPHA,
	TEXTURE_TRANSPARENT,
	COLOR_SOLID,
	COLOR_TRANSPARENT
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
	u32 color; // RGB
	u8 opacity;
	u32 offset;
	u32 texColOffset;
	float texColDark;
	u32 size;
	u32 texWidth;
	u32 texHeight;
	MaterialType type;
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
	u8 data[8];
} f3d;

typedef struct {
	u8 len;
	u8 data[0x20];
} geoCmd;

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

typedef struct {
	u16 type;
	s16 x1;
	s16 z1;
	s16 x2;
	s16 z2;
	s16 y;
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
bool createSolidDL = false;
bool createAlphaDL = false;
bool createTransDL = false;
bool definedSegPtr = false;
MaterialType lastTextureType = NOT_DEFINED;
string geoModeData = "", colorTexData = "";

/* Tool Variables */
string romPath;
string collisionString;
u32 romPos = 0xFFFFFFFF;
u8 curSeg = 0;
u32 startSegOffset = 0;
u32 curSegOffset = 0;
bool flipTexturesVertically = false;
bool centerVerts = false;
float modelScale = 1.0f;
s16 xOff = 0;
s16 yOff = 0;
s16 zOff = 0;
bool enableFog = false;
FogType fogType = FOG_VERY_INTENSE;
u8 fogRed = 0xFF, fogGreen = 0xFF, fogBlue = 0xFF;
u8 defaultColor[16] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x7F,0x7F,0xFF,0x7F,0x7F,0x7F,0xFF };
bool enableDeathFloor = true;
s16 dfx1 = 0x4000, dfz1 = 0x4000, dfx2 = 0xBFFF, dfz2 = 0xBFFF, dfy = -8000;
bool enableWaterBoxes = false;
vector<waterBox> waterBoxes;

void importOBJCollision(const char*, const char*, bool);

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
	//printf("Adding an objPtr: %s\n", name);
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
				//printf("Adding a ptr: 0x%X\n", data);
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
			//printf("Set geomtry mode for '%s', value = 0x%X\n", m.name.c_str(), m.geoMode);
			return;
		}
	}
}

void checkColorTexInfo(material& m) {
	m.color = 0;
	m.texColOffset = 0;
	m.texColDark = 0.8f;
	vector<string> gma = splitString(colorTexData, ",");
	for (string gme : gma) {
		vector<string> gmd = splitString(gme, ":");
		if (m.name.compare(gmd[0]) == 0) {
			if (gmd[1].find("0x") == 0)
				m.color = stoll(gmd[1].substr(2).c_str(), 0, 16);
			else if (gmd[1].find("$") == 0) {
				m.color = stoll(gmd[1].substr(1).c_str(), 0, 16);
			}
			else
				m.color = stoll(gmd[1]);
			m.enableTextureColor = true;
			//printf("Set texture color for '%s', value = 0x%X\n", m.name.c_str(), m.color);
			if (gmd.size() > 2) 
				m.texColDark = stof(gmd[2]);
			return;
		}
	}
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
			m.hasTransparency = false;
			m.isTextureCopy = false;
			m.name = line.substr(7);
			m.collision = 0;
			m.opacity = 0xFF;
			m.enableGeoMode = false;
			m.enableTextureColor = false;
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
			materials.at(materials.size() - 1).opacity = stof(line.substr(2).c_str()) * 0xFF;
			processMaterialColorAlpha(stof(line.substr(2).c_str()), &materials.at(materials.size() - 1));
		}
		else if (line.find("map_Kd ") == 0) {
			//cout << "Found image: " << line << endl;
			size = processRGBAImage(line.substr(7).c_str(), &materials.at(materials.size()-1));
		}
	}
	if (!materials.at(materials.size() - 1).isTextureCopy) {
		(&materials.at(materials.size() - 1))->offset = lastOffset;
		(&materials.at(materials.size() - 1))->size = size;
	}if (materials.at(materials.size() - 1).enableTextureColor) {
		lastOffset += 0x10;
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

geoCmd strGeo(string str) {
	geoCmd cmd;
	vector<string> b = splitString(str, " ");
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
		u32 off = startSegOffset + addOffset + mat.offset + 0x10;
		char cmd[24];
		sprintf(cmd, "03 86 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		//printHexBytes(strF3D(cmd).data, 8);
		off = startSegOffset + addOffset + mat.offset + 0x18;
		sprintf(cmd, "03 88 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		//printHexBytes(strF3D(cmd).data, 8);
	}
	else {
		u32 off = startSegOffset;
		if (mat.enableTextureColor) 
			off = startSegOffset + addOffset + mat.texColOffset;
		char cmd[24];
		sprintf(cmd, "03 86 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		//printHexBytes(strF3D(cmd).data, 8);
		off = startSegOffset + 8;
		if (mat.enableTextureColor)
			off = startSegOffset + addOffset + mat.texColOffset + 0x8;
		sprintf(cmd, "03 88 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
		cmds.push_back(strF3D(cmd));
		//printHexBytes(strF3D(cmd).data, 8);
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
	int lineSize = mat.texWidth / 2;
	if (first) {
		if(mat.type == TEXTURE_SOLID || mat.type == TEXTURE_ALPHA || mat.type == TEXTURE_TRANSPARENT)
			cmds.push_back(strF3D("F5 10 00 00 07 00 00 00"));
	} else {
		if (mat.type == TEXTURE_SOLID || mat.type == TEXTURE_ALPHA || mat.type == TEXTURE_TRANSPARENT) {
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
		if (mat.hasTextureAlpha)
			if(enableFog)
				cmds.push_back(strF3D("FC FF FF FF FF FC F2 38"));
			else
				cmds.push_back(strF3D("FC 12 18 24 FF 33 FF FF"));
		else if(mat.type == TEXTURE_TRANSPARENT)
			cmds.push_back(strF3D("FC 12 2E 24 FF FF FB FD"));
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
		sprintf(cmd, "FD 10 00 00 %X %X %X %X", curSeg & 0xFF, (off >> 16) & 0xFF, (off >> 8) & 0xFF, off & 0xFF);
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
	//printf("Color: r=0x%X g=0x%X b=0x%X a=0x%X\n",r,g,b,a);
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

	addObjPtr((char*)objPath);

	string line;
	ifstream infile;
	infile.open(objPath);
	while (!infile.eof()) {
		getline(infile, line);
		processLine(line);
	}
	centerVertices();
	processFaces();
	infile.close();
	//printf("Import Size: 0x%x\n", 0x10+getCombinedTextureSizes()+getCombinedVertSizes());

	if (curSegOffset % 16 != 0) curSegOffset += 16 - (curSegOffset % 16); // DEBUG (Add padding)

	addPtr((char*)objPath, PTR_START, (curSeg << 24) | curSegOffset);
	int importStart = romPos + curSegOffset;
	printf("importStart: 0x%X\n", importStart);
	curSegOffset += 0x10;
	for (material& mt : materials) {
		//printf("material '%s' offset = 0x%x, size = 0x%x\n", mt.name.c_str(), mt.offset, mt.size);
		if (!mt.isTextureCopy) curSegOffset += mt.size;
		if (mt.enableTextureColor) {
			mt.texColOffset = curSegOffset;
			//printf("Texture '%s' Color: 0x%X\n", mt.name.c_str(), mt.color);
			curSegOffset += 0x10;
		}
	}

	int startVerts = curSegOffset;
	for (finalVertex fv : combinedVerts) {
		//printHexBytes(fv.data, 16); // Vertices
		curSegOffset += 0x10;
	}
	//printf("Number of tri: %X\n",((curSegOffset-startVerts)/0x10)/3);
	// We need to seperate the model into at most 3 seperate DLs.
	vector<f3d> solidCmds; // For textures/color with no transparency in them. (Layer 1)
	vector<f3d> alphaCmds; // For textures that have atleast 1 alpha bit in them. (Layer 4)
	vector<f3d> transCmds; // For textures/colors that have transparency in them. (Layer 5)

	printf("F3D solid starts at 0x%X\n", curSegOffset);
	addPtr((char*)objPath, PTR_SOLID, (curSeg << 24) | curSegOffset);
	solidCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("B7 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("BB 00 00 01 FF FF FF FF"));
	solidCmds.push_back(strF3D("E8 00 00 00 00 00 00 00"));
	solidCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
	if (enableFog) addFogStart(solidCmds, false);
	for (int i = 0; i < matPos.size(); ++i) {
		materialPosition mp = matPos.at(i);
		if (mp.mat->hasTextureAlpha || mp.mat->hasTransparency) continue;
		if (mp.mat->enableGeoMode) {
			solidCmds.push_back(strF3D("B6 00 00 00 FF FF FF FF"));
			char cmd[24];
			sprintf(cmd, "B7 00 00 00 %X %X %X %X",
				(mp.mat->geoMode >> 24) & 0xFF,
				(mp.mat->geoMode >> 16) & 0xFF,
				(mp.mat->geoMode >> 8) & 0xFF,
				mp.mat->geoMode & 0xFF
				);
			solidCmds.push_back(strF3D(cmd));
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
			}
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
		addPtr((char*)objPath, PTR_TRANS, (curSeg << 24) | curSegOffset);
		transCmds.push_back(strF3D("E7 00 00 00 00 00 00 00"));
		transCmds.push_back(strF3D("B7 00 00 00 00 00 00 00"));
		transCmds.push_back(strF3D("BB 00 00 01 FF FF FF FF"));
		transCmds.push_back(strF3D("E8 00 00 00 00 00 00 00"));
		transCmds.push_back(strF3D("E6 00 00 00 00 00 00 00"));
		for (int i = 0; i < matPos.size(); ++i) {
			materialPosition mp = matPos.at(i);
			if (mp.mat->hasTextureAlpha || !mp.mat->hasTransparency) continue;

			//printf("Comparing Types for texture '%s' (TRANS) %d != %d\n", mp.mat->name.c_str(), lastTextureType, mp.mat->type);
			if (lastTextureType != mp.mat->type) {
				if (mp.mat->type == TEXTURE_TRANSPARENT) {
					char cmd[24];
					sprintf(cmd, "FB 00 00 00 FF FF FF %X", mp.mat->opacity);
					transCmds.push_back(strF3D(cmd));
					resetBF = true;
				}
				//printf("Current type: %d\n", mp.mat->type);
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
							addCmd03(solidCmds, *mp.mat, importStart - romPos);
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
			int length;
			//printf("Material '%s': %d \n", mp.mat->name.c_str(), mp.position);
			if (i < matPos.size() - 1) length = matPos.at(i + 1).position - mp.position;
			else length = currentFace - mp.position;
			//printf("Length: %d \n", length);
			for (int l = 0; l < length; l++) {
				if (l % 5 == 0) {
					addCmd04(transCmds, *mp.mat, length - l, startVerts + ((mp.position * 3 + l * 3) * 0x10));
				}
				addCmdBF(transCmds, *mp.mat, l % 5);
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
	//printf("Total Size = 0x%X\n", size);

	u8 * importData = (u8*)malloc(size);
	memcpy(importData, defaultColor, 0x10);
	int curOff = 0x10;
	for (material mt : materials) {
		//printf("material '%s' offset = 0x%x, size = 0x%x\n", mt.name.c_str(), mt.offset, mt.size);
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
	resetVariables();
}

void importOBJCollision(const char* objPath, const char* col_data, bool continued) {
	if (!continued) {
		addObjPtr((char*)objPath);
		resetVariables();
		string line;
		ifstream infile;
		infile.open(objPath);
		while (!infile.eof()) {
			getline(infile, line);
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
		//printf("Material (CLSN) '%s': %d \n", mp->mat->name.c_str(), mp->position);
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

	if (enableWaterBoxes && waterBoxes.size() > 0) {
		collision.push_back(0x0044);
		collision.push_back(waterBoxes.size());
		curSegOffset += 4;
		for (waterBox& wb : waterBoxes) {
			collision.push_back(wb.type);
			collision.push_back(wb.x1);
			collision.push_back(wb.z1);
			collision.push_back(wb.x2);
			collision.push_back(wb.z2);
			collision.push_back(wb.y);
			curSegOffset += 12;
		}
	}

	collision.push_back(0x0042);
	curSegOffset += 4;
	addPtr((char*)objPath, PTR_COL, (curSeg << 24) | collisionStart);
	//printHexShorts((u16*)&collision[0], collision.size());
	printf("Collision starts at 0x%X\n", collisionStart);

	for (u16& col : collision) col = _byteswap_ushort(col);
	//printf("Collision length = %X\n",collision.size()*2);

	if (curImp->curPos + collision.size()*2 <= curImp->limit) {
		memcpy(curImp->data + curImp->curPos, (u8*)&collision[0], collision.size() * 2);
		curImp->curPos += collision.size() * 2;
	}
	else {
		err.id = 100;
		err.message = "Import data limit has been exceeded!";
	}

}

void importGeoLayout(const char* objPathName, bool forLevel) {
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
			cmds.push_back(strGeo("0A 01 00 2D 00 64 96 96 80 29 AA 3C"));
			cmds.push_back(strGeo("04 00 00 00"));
			cmds.push_back(strGeo("0F 00 00 01 00 00 07 D0 17 70 0C 00 00 00 EE 00 80 28 7D 30"));
			cmds.push_back(strGeo("04 00 00 00"));
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
}

void importBin(const char* binPath, int offset, int setSize) {
	FILE* bf = fopen(binPath, "rb");
	int size;
	if (setSize > -1) size = setSize;
	else {
		fseek(bf, 0, SEEK_END);
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
	int size = (strlen(hexString)+1)/3;
	u8 * hexData = (u8*)malloc(size);
	//printf("Number of hex bytes: %X\n", size);
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
	/*
	printf("First two: 0x%X\n", (hexData[0] << 8) | hexData[1]);
	for (int i = 0; i < size; i += 2) {
		printf("Before: 0x%X\n", (hexData[i] << 8) | hexData[i + 1]);
		swap(hexData[i], hexData[i + 1]);
		printf("After: 0x%X\n", (hexData[i] << 8) | hexData[i + 1]);
	}*/
	
	memcpy(curImp->data+curImp->curPos, hexData, size);
	curImp->curPos += size;
	free(hexData);
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
		value = stoi(input.substr(2), 0, 16);
	else if (input.find("$") == 0)
		value = stoi(input.substr(1), 0, 16);
	else
		value = stoi(input);
}

void importDataIntoRom() {
	//return;
	try {
		FILE* ROM = fopen(romPath.c_str(), "rb+");
		for (import& i : importData) {
			//printHexBytes(i.data, i.curPos);
			fseek(ROM, i.address, 0);
			fwrite(i.data, 1, i.curPos, ROM);
			free(i.data); // Free all data
		}
		importData.clear();
		for (putData put : putImports) {
			fseek(ROM, put.address, 0);
			u32 pd = _byteswap_ulong(put.data);
			fwrite(&pd, 4, 1, ROM);
			//printf("put 0x%X at ROM address 0x%X\n", put.data, put.address);
		}
		putImports.clear();
		//printf("tweaks.size() = %d\n", tweaks.size());
		if (tweaks.size() > 0) {
			//printf("Importing Tweaks into ROM!\n");
			for (tweakPatch tp : tweaks){
				//printf("Rom Address = 0x%X\n", tp.address);
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

/*
FIX WATER BOXES!

waterBox wb;
wb.type = 0;
wb.x1 = -0x2000;
wb.z1 = -0x2000;
wb.x2 = 0x2000;
wb.z2 = 0x2000;
wb.y = 0;
waterBoxes.push_back(wb);
*/

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

			//printf("Importing to ROM file: %s\n", romPath.c_str());
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
				//printf("Rom Position: %X, Segment: %X, Offset: %X\n", romPos, curSeg, curSegOffset);
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
			modelScale = stof(arg[1]);
			//printf("Model scale size set to: %f\n", modelScale);
		}
		else if (cmd.compare("-o") == 0 && arg.size() > 3) {
			parseIntValue(arg[1], (int&)xOff);
			parseIntValue(arg[2], (int&)yOff);
			parseIntValue(arg[3], (int&)zOff);
		} // 
		else if (cmd.compare("-sv") == 0 && arg.size() > 2) {
			u32 light, dark;
			parseIntValue(arg[1], (int&)light);
			parseIntValue(arg[2], (int&)dark);
			setLightAndDarkValues(light, dark);
		}
		else if (cmd.compare("-c") == 0 && arg.size() > 1) {
			if (curImp != NULL) {
				parseIntValue(arg[1], (int&)curImp->limit);
				realloc((u8*)curImp->data, curImp->limit);
				//printf("Data limit set to: 0x%X\n", curImp->limit);
			}
			else {
				parseIntValue(arg[1], (int&)setNextLimit);
				//printf("Data limit set to: 0x%X\n", setNextLimit);
			}
		}
		else if (cmd.compare("-io") == 0 && arg.size() > 1) {
			if (romPath.empty()) {
				err.id = 2;
				err.message = "You have not defined a path to the ROM file yet!";
				continue;
			}
			else if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (definedSegPtr) {
				if (arg.size() == 2) {
					importOBJ(arg[1].c_str(), NULL);
				}
				else if (arg.size() > 2) {
					importOBJ(arg[1].c_str(), arg[2].c_str());
				}
			}
			else {
				err.id = 4;
				err.message = "You did not define a segmented pointer!\n";
			}
		}
		else if (cmd.compare("-ioc") == 0) {
			if (romPath.empty()) {
				err.id = 2;
				err.message = "You have not defined a path to the ROM file yet!";
				continue;
			}
			else if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (arg.size() > 2)
				importOBJCollision(arg[1].c_str(), arg[2].c_str(), false);
		}
		else if (cmd.compare("-ig") == 0) {

			if (romPath.empty()) {
				err.id = 2;
				err.message = "You have not defined a path to the ROM file yet!";
				continue;
			}
			else if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (arg.size() > 2) {
				//printf("Adding geo layout! Args: %s, %s\n", arg[1].c_str(), arg[2].c_str());
				importGeoLayout(arg[1].c_str(), stoi(arg[2].c_str()) > 0 ? true : false);
			}
		}
		else if (cmd.compare("-ih") == 0) {
			if (romPath.empty()) {
				err.id = 2;
				err.message = "You have not defined a path to the ROM file yet!";
				continue;
			}
			else if (romPos == 0xFFFFFFFF) {
				err.id = 3;
				err.message = "You have not defined a rom position yet!";
				continue;
			}
			if (arg.size() > 1)
				importHexData((char*)trim(arg[1]).c_str());
		}
		else if (cmd.compare("-it") == 0) {
			if (arg.size() > 1)
				importTweak(arg[1]);
		}
		else if (cmd.compare("-ib") == 0) {
			if (romPath.empty()) {
				err.id = 2;
				err.message = "You have not defined a path to the ROM file yet!";
				continue;
			}
			else if (romPos == 0xFFFFFFFF) {
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
		else if (cmd.compare("-vft") == 0 && arg.size() > 1) {
			arg[1] = tolowercase(arg[1]);
			if (arg[1].compare("t") == 0 || arg[1].compare("1") == 0 || arg[1].compare("true") == 0)
				flipTexturesVertically = true;
			if (arg[1].compare("f") == 0 || arg[1].compare("0") == 0 || arg[1].compare("false") == 0)
				flipTexturesVertically = false;
			//printf("Am I flipping the textures vertically? %s\n", flipTexturesVertically ? "Yes I am!" : "No I'm not");
		}
		else if (cmd.compare("-center") == 0 && arg.size() > 1) {
			arg[1] = tolowercase(arg[1]);
			if (arg[1].compare("t") == 0 || arg[1].compare("1") == 0 || arg[1].compare("true") == 0)
				centerVerts = true;
			if (arg[1].compare("f") == 0 || arg[1].compare("0") == 0 || arg[1].compare("false") == 0)
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

	if (err.id)
		printf("Error %d: %s\n", err.id, err.message);
	else 
		importDataIntoRom();
	
	return err.id;
}