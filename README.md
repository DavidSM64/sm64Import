# sm64Import

sm64Import v0.0 (Work-in-progress, about 90% done)

Current Commands:
[-a ROM_ADDRESS {SEG_POINTER}] Set the base ROM address (and optional segmented pointer) to import to. The segmented pointer is required for -io
[-c SIZE] Sets the max data cap. If the following data is larger than this, then the program will return an error.
[-ct DATA] Allows you to add colors textures. Useful for crystals.
-ct "MaterialName:0xFF0000FF"
[-ib BIN_PATH {OFFSET SIZE}] Import binary file into the ROM. Optional parameters for importing only a specific section of the file into the ROM.
[-ig OBJ_NAME GEO_TYPE] Creates a geo layout from data from a previous -io cmd. GEOTYPE = 0 for objects & GEOTYPE = 1 for levels.
[-center BOOLEAN] Centers the current model/collision if true. Used with -io & -ioc.
[-ih HEX_STRING] Import raw hex from a string into the ROM
example: -ih "FF 00 00 FF FF 00 00 FF"
[-io OBJ_PATH {COL_DATA}] Import .obj file as f3d level data (Textures, colors, verticies, and f3d scripts). 
If you define the optional second parameter, {COL_DATA}, then collision data will be generated for the OBJ.
[-ioc OBJ_PATH COL_DATA] Import .obj file as collision data. COL_DATA is for defining each material with a collision type. 
Note: You don't have to define every material with COL_DATA, the default value is set to 0 (enviorment default) for each material.
[-it XML_PATH] Imports a tweak file into the ROM.
[-o OFFSET_X OFFSET_Y OFFSET_Z] Offset model/collision data. Used with -io & -ioc.
[-fog ENABLED {FOG_TYPE RED_VALUE GREEN_VALUE BLUE_VALUE} ] Enables/Disables fog in the current level. Used only with -io.
[-r ROM_PATH] ROM to import data to
[-run PROGRAM] Runs a exe program.
[-s SCALE_AMOUNT] Scale factor. Used with -io & -ioc.
[-sv LIGHT_VALUE DARK_VALUE] Shading value for levels. Used with -io.
[-sgm DATA] Sets the geometry mode for the following -io cmd.
-sgm "MaterialName:0x00040000"
[-put OBJ_NAME SEG_DATA ROM_ADDRESS]
SEG_DATA can be (Names are not case-sensative): START, SOLID, ALPHA, TRANS, GEO, or COL
[-vft BOOLEAN] Flip all textures vertically if true. Used with -io.

Overwriting BOB example (You're expected to setup the level scripts yourself, which you can import as a binary file):
sm64Import.exe -r "C:/Users/David/Desktop/sm64.ext.z64" -a 0x2AC0F8 -ih "00 10 00 19 01 8E 00 00 01 8E 20 00 19 00 00 1C" -a 0x18E0000 0x19000000 -c 0x1500 -ib "LevelScript.bin" -a 0x18f0000 0x0E000000 -c 0x150000 -s 500 -io "16x16Test.obj" "" -a 0x18E1700 0x19001700 -c 0x100 -ig "16x16Test.obj" 1 -put "16x16Test.obj" col 0x18E0374
