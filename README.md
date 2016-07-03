# sm64Import v0.1

This tool can be used to import .obj files, binary files, hex data, and tweaks into the Super Mario 64 ROM.

Libraries used:
- lodepng.h - Used to decode .png files for textures
- jpeg_decode.h - Used to decode .jpg files for textures
- BMP.h - Used to decode .bmp files for textures
- rapidXML.h - Used to parse .xml files for importing tweak files.

Preparing your ROM:
- Use ApplyPPF3.exe to apply the obj_import195S ppf patch.
- beh12-hack.bin & cmd17-hack.bin are both required to get a custom level to work
- water-hack.bin is required for water boxes
- LevelImporterPatches.xml tweak is used for minor improvements

<code>sm64Import.exe -run "hacks/tools/ApplyPPF3.exe a "sm64.ext.z64" "obj_import195S.ppf"" -r "sm64.ext.z64" -a 0x101B84 -ib "hacks/beh12-hack.bin" -a 0x1202000 -ib "hacks/cmd17-hack.bin" -a 0x1203000 -ib "hacks/water-hack.bin" -it "tweaks/LevelImporterPatches.xml"</code>

Importing over BOB example ([Using this basic LevelScript.bin file](https://drive.google.com/uc?export=download&id=0B0ipn7N-yey8VkJDWnZrWC14M2s)):
<br />
<code>sm64Import.exe -df t 0x4000 0x4000 0xBFFF 0xBFFF -8000 -r "sm64.ext.z64" -a 0x2AC0F8 -ih "00 10 00 19 01 8E 00 00 01 8E 20 00 19 00 00 1C" -a 0x18E1800 0x19001800 -c 0x1500 -ib "LevelScript.bin" -a 0x18F0000 0x0E000000 -c 0x150000 -s 500 -io "Import.obj" "" -a 0x18E1700 0x19001700 -c 0x100 -ig "Import.obj" 1 -put "Import.obj" col 0x18E014C</code>

Arguments:
 - <code>-a ROM_ADDRESS {SEG_POINTER}</code> Set the base ROM address (and optional segmented pointer) to import to. The segmented pointer is required for importing obj files.
 - <code>-c SIZE</code> Sets the max data cap. If the following import data size is larger than this, then the program will return an error.
 - <code>-center BOOLEAN</code> Centers the follwing obj model(s) if true.
 - <code>-ct DATA</code> Allows you to add color to textures. DATA = <code>"Name:Color:DarkScale"</code>. DarkScale is a float multiplier from the original color value that is used for lighting, so 0.8 would be 80% as bright as the set color.<br /> Example: <code>-ct "TextureName:0xFF0000FF:0.8"</code>
 - <code>-df BOOLEAN X1_POS Z1_POS X2_POS Z2_POS Y_POS</code> Creates a death floor for the level.
 - <code>-ib BIN_PATH {OFFSET SIZE}</code> Import binary file into the ROM. Optional parameters for importing only a specific section of the file into the ROM.  Example: <code>-ib "A_Binary_File.bin" 0x1000 0x10</code>
 - <code>-ig OBJ_NAME GEO_TYPE</code> Creates a geo layout from data from a previous -io cmd. GEOTYPE = 0 for objects & GEOTYPE = 1 for levels.
 - <code>-ih HEX_STRING</code> Import raw hex from a string into the ROM. Example: <code>-ih "FF 00 00 FF FF 00 00 FF"</code>
 - <code>-io OBJ_PATH {COL_DATA}</code> Import .obj file as f3d level data (Textures, colors, verticies, and f3d scripts). If you define the optional second parameter, {COL_DATA}, then collision data will be generated for the OBJ.
 - <code>-ioc OBJ_PATH COL_DATA</code> Import .obj file as collision data. COL_DATA is for defining each material with a collision type. Note: You don't have to define every material with COL_DATA, the default value is set to 0 (enviorment default) for each material.
 - <code>-it XML_PATH</code> Imports a tweak file into the ROM.
 - <code>-o OFFSET_X OFFSET_Y OFFSET_Z</code> Set offset model/collision data.
 - <code>-option OPTION VALUE</code> Changes an importer value. Currently the only option to change is the vertex reduction level. Example: <code>-option rvl 2</code> will set the vertex reduction level to 2.
 - <code>-fog ENABLED {FOG_TYPE RED_VALUE GREEN_VALUE BLUE_VALUE}</code> Enables/Disables fog in the current level.
 - <code>-r ROM_PATH</code> Defines the ROM to import data to
 - <code>-run PROGRAM</code> Runs a exe program with arguments.
 - <code>-s SCALE_AMOUNT</code> Scale factor for model.
 - <code>-sv LIGHT_VALUE DARK_VALUE</code> Set shading value for levels.
 - <code>-sgm DATA</code> Sets the geometry mode for a particular material. Example: <code>-sgm "TextureName:0x00040000"</code>
 - <code>-tt DATA {EXTRA}</code> Allows you to define the type of texture a material is, default type is rgba16. Example: <code>-tt "TextureName:rgba32"</code>. The currently supported texture types are: RGBA16, RGBA32, I4, I8, IA4, IA8, and IA16. You can make I4 & I8 textures have an alpha mask by setting the optional {EXTRA} parameter to be "a" like this: <code>-tt "i4_tex:i8:a"</code>
 - <code>-put OBJ_NAME SEG_DATA ROM_ADDRESS</code> Puts the segmented pointer of SEG_Data into the ROM. Useful for updating pointers in your levelscript automatically. SEG_DATA can be (Names are not case-sensative): START, SOLID, ALPHA, TRANS, GEO, or COL
 - <code>-vft BOOLEAN</code> Flip all textures vertically if true.
 - <code>-wb BOX_TYPE_ID X1_POS Z1_POS X2_POS Z2_POS HEIGHT</code> Creates a water box in the level. Used with -io* & -ioc. *Water box data is grouped with collision data, so you have to define <COL_DATA> if your just using -io.
 - <code>-wbc</code> Clears the water box list.
 - <code>-wd</code> Imports water data for the current level. Note: You MUST import this at the segmented address 0x19001800 with the current patches.
