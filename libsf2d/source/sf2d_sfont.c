#include <stdlib.h>
#include <string.h>

#include <citro3d.h>

#include "sf2d.h"

typedef struct{
	const char* str;
	sf2d_txt_vertex_s* textVtxArray;
} text_t;

static C3D_Tex* glyphSheets;
static int textVtxArrayPos;
static C3D_Mtx projection;
static int num_strs = 0;
text_t* texts = NULL;
static bool matchFound;
sf2d_txt_vertex_s* textVtxArray;

#define TEXT_VTX_ARRAY_COUNT (4*1024)

void sf2d_sfont_init(void)
{
	fontEnsureMapped();

	// Load the glyph texture sheets
	int i;
	TGLP_s* glyphInfo = fontGetGlyphInfo();
	glyphSheets = malloc(sizeof(C3D_Tex)*glyphInfo->nSheets);
	for (i = 0; i < glyphInfo->nSheets; i ++)
	{
		C3D_Tex* tex = &glyphSheets[i];
		tex->data = fontGetGlyphSheetTex(i);
		tex->fmt = glyphInfo->sheetFmt;
		tex->size = glyphInfo->sheetSize;
		tex->width = glyphInfo->sheetWidth;
		tex->height = glyphInfo->sheetHeight;
		tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
			| GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
	}
}

void sf2d_sfont_fini(void)
{
	// Free the textures
	free(glyphSheets);
}

static void _sf2d_sfont_add_text_vertex(float vx, float vy, float tx, float ty)
{
	sf2d_txt_vertex_s* vtx = &textVtxArray[textVtxArrayPos++];
	vtx->position[0] = vx;
	vtx->position[1] = vy;
	vtx->position[2] = 0.5f;
	vtx->texcoord[0] = tx;
	vtx->texcoord[1] = ty;
}

void sf2d_sfont_draw_text(float x, float y, float size, u32 color, const char* text){
	ssize_t  units;
	uint32_t code;

	// Compute the projection matrix
	Mtx_OrthoTilt(&projection, 0.0, 400.0, 240.0, 0.0, 0.0, 1.0, true);
	Mtx_OrthoTilt(&projection, 0.0, 320.0, 240.0, 0.0, 0.0, 1.0, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_desc, &projection);

	int i;
	matchFound = false;
	for(i = 0; i < num_strs; i++){
		text_t text_inst = texts[i];
		if(strcmp(text, text_inst.str) == 0){
			//Match found, don't allocate more memory!
			matchFound = true;
			break;
		}
	}
	if(matchFound == false){
		//allocate space for new string
		if(texts == NULL)
			texts = malloc(sizeof(text_t));
		else
			texts = realloc(texts, (num_strs+1)*sizeof(text_t));
		num_strs++;
		texts[num_strs-1].str = text;
		// Create the text vertex array
		textVtxArray = (sf2d_txt_vertex_s*)linearAlloc(sizeof(sf2d_txt_vertex_s)*TEXT_VTX_ARRAY_COUNT);
		texts[num_strs-1].textVtxArray = textVtxArray;
	}
	else{
		textVtxArray = texts[i].textVtxArray;
	}
	textVtxArrayPos = 0;
	
	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2); // v1=texcoord

	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, textVtxArray, sizeof(sf2d_txt_vertex_s), 2, 0x10);

	const uint8_t* p = (const uint8_t*)text;
	float firstX = x;
	u32 flags = GLYPH_POS_CALC_VTXCOORD;
	int lastSheet = -1;
	do
	{
		if (!*p) break;
		units = decode_utf8(&code, p);
		if (units == -1)
			break;
		p += units;
		if (code == '\n')
		{
			x = firstX;
			y += size*fontGetInfo()->lineFeed;
		}
		else if (code > 0)
		{
			int glyphIdx = fontGlyphIndexFromCodePoint(code);
			fontGlyphPos_s data;
			fontCalcGlyphPos(&data, glyphIdx, flags, size, size);

			// Bind the correct texture sheet
			if (data.sheetIndex != lastSheet)
			{
				lastSheet = data.sheetIndex;
				C3D_TexBind(0, &glyphSheets[lastSheet]);
				C3D_TexEnv* env = C3D_GetTexEnv(0);
				C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, 0, 0);
				C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT, 0);
				C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
				C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
				C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
				C3D_TexEnvColor(env, color);
			}

			int arrayIndex = textVtxArrayPos;
			if ((arrayIndex+4) >= TEXT_VTX_ARRAY_COUNT)
				break; // We can't render more characters

			// Add the vertices to the array
			_sf2d_sfont_add_text_vertex(x+data.vtxcoord.left,  y+data.vtxcoord.bottom, data.texcoord.left,  data.texcoord.bottom);
			_sf2d_sfont_add_text_vertex(x+data.vtxcoord.right, y+data.vtxcoord.bottom, data.texcoord.right, data.texcoord.bottom);
			_sf2d_sfont_add_text_vertex(x+data.vtxcoord.left,  y+data.vtxcoord.top,    data.texcoord.left,  data.texcoord.top);
			_sf2d_sfont_add_text_vertex(x+data.vtxcoord.right, y+data.vtxcoord.top,    data.texcoord.right, data.texcoord.top);

			// Draw the glyph
			C3D_DrawArrays(GPU_TRIANGLE_STRIP, arrayIndex, 4);

			x += data.xAdvance;

		}
	} while (code > 0);
}
