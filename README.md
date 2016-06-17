# sm64Import

sm64Import v0.0 (Work-in-progress, about 90% done)

Uses two libraries:<br />
lodepng - Used to decode .png files for RGBA16 textures<br />
rapidXML - Used to parse .xml files for importing tweak files.<br />

Current Commands:<br />
[-a ROM_ADDRESS {SEG_POINTER}] Set the base ROM address (and optional segmented pointer) to import to. The segmented pointer is required for -io<br />
[-c SIZE] Sets the max data cap. If the following data is larger than this, then the program will return an error.<br />
[-ct DATA] Allows you to add colors textures. Useful for crystals.<br />
-ct "MaterialName:0xFF0000FF"<br />
[-ib BIN_PATH {OFFSET SIZE}] Import binary file into the ROM. Optional parameters for importing only a specific section of the file into the ROM.<br />
[-ig OBJ_NAME GEO_TYPE] Creates a geo layout from data from a previous -io cmd. GEOTYPE = 0 for objects & GEOTYPE = 1 for levels.<br />
[-center BOOLEAN] Centers the current model/collision if true. Used with -io & -ioc.<br />
[-ih HEX_STRING] Import raw hex from a string into the ROM<br />
example: -ih "FF 00 00 FF FF 00 00 FF"<br />
[-io OBJ_PATH {COL_DATA}] Import .obj file as f3d level data (Textures, colors, verticies, and f3d scripts). <br />
If you define the optional second parameter, {COL_DATA}, then collision data will be generated for the OBJ.<br />
[-ioc OBJ_PATH COL_DATA] Import .obj file as collision data. COL_DATA is for defining each material with a collision type. <br />
Note: You don't have to define every material with COL_DATA, the default value is set to 0 (enviorment default) for each material.<br />
[-it XML_PATH] Imports a tweak file into the ROM.<br />
[-o OFFSET_X OFFSET_Y OFFSET_Z] Offset model/collision data. Used with -io & -ioc.<br />
[-fog ENABLED {FOG_TYPE RED_VALUE GREEN_VALUE BLUE_VALUE} ] Enables/Disables fog in the current level. Used only with -io.<br />
[-r ROM_PATH] ROM to import data to<br />
[-run PROGRAM] Runs a exe program.<br />
[-s SCALE_AMOUNT] Scale factor. Used with -io & -ioc.<br />
[-sv LIGHT_VALUE DARK_VALUE] Shading value for levels. Used with -io.<br />
[-sgm DATA] Sets the geometry mode for the following -io cmd.<br />
-sgm "MaterialName:0x00040000"<br />
[-put OBJ_NAME SEG_DATA ROM_ADDRESS]<br /> Puts the segmented pointer of SEG_Data into the ROM. Useful for updating pointers in your levelscript automatically.
SEG_DATA can be (Names are not case-sensative): START, SOLID, ALPHA, TRANS, GEO, or COL<br />
[-vft BOOLEAN] Flip all textures vertically if true. Used with -io.<br />

Overwriting BOB example (You're expected to setup the level scripts yourself, which you can import as a binary file):
sm64Import.exe -r "C:/Users/David/Desktop/sm64.ext.z64" -a 0x2AC0F8 -ih "00 10 00 19 01 8E 00 00 01 8E 20 00 19 00 00 1C" -a 0x18E0000 0x19000000 -c 0x1500 -ib "LevelScript.bin" -a 0x18f0000 0x0E000000 -c 0x150000 -s 500 -io "16x16Test.obj" "" -a 0x18E1700 0x19001700 -c 0x100 -ig "16x16Test.obj" 1 -put "16x16Test.obj" col 0x18E0374
