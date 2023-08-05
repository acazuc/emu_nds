#ifndef GPU_H
#define GPU_H

#include <stdint.h>

#define TRANSFORM_INT28(n) \
do \
{ \
	if ((n) & (1 << 27)) \
	{ \
		n = (0x7FFFFFF - ((n) & 0x7FFFFFF)); \
		n = -n - 1; \
	} \
} while (0)

#define PRIMITIVE_TRIANGLES      0
#define PRIMITIVE_QUADS          1
#define PRIMITIVE_TRIANGLE_STRIP 2
#define PRIMITIVE_QUAD_STRIP     3

struct mem;

struct gpu_eng
{
	uint8_t *data;
	uint32_t pitch;
	uint8_t  (*get_vram_bg8 )(struct mem *mem, uint32_t addr);
	uint16_t (*get_vram_bg16)(struct mem *mem, uint32_t addr);
	uint32_t (*get_vram_bg32)(struct mem *mem, uint32_t addr);
	uint8_t  (*get_vram_obj8 )(struct mem *mem, uint32_t addr);
	uint16_t (*get_vram_obj16)(struct mem *mem, uint32_t addr);
	uint32_t (*get_vram_obj32)(struct mem *mem, uint32_t addr);
	uint32_t reg_base;
	uint32_t pal_base;
	uint32_t oam_base;
	int32_t bg2x;
	int32_t bg2y;
	int32_t bg3x;
	int32_t bg3y;
	int engb;
};

struct vec4
{
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t w;
};

struct vec3
{
	int32_t x;
	int32_t y;
	int32_t z;
};

struct vec2
{
	int32_t x;
	int32_t y;
};

struct matrix
{
	struct vec4 x;
	struct vec4 y;
	struct vec4 z;
	struct vec4 w;
};

struct vertex
{
	struct vec4 position;
	struct vec3 normal;
	struct vec2 texcoord;
	int32_t screen_x;
	int32_t screen_y;
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct polygon
{
	uint8_t quad;
	uint32_t attr;
	uint16_t vertexes[4];
};

struct gpu_g3d_buf
{
	uint8_t data[256 * 192 * 4];
	struct vertex vertexes[6144];
	struct polygon polygons[2048];
	uint16_t vertexes_nb;
	uint16_t polygons_nb;
};

struct gpu_g3d
{
	struct gpu_g3d_buf bufs[2];
	struct gpu_g3d_buf *front;
	struct gpu_g3d_buf *back;
	struct matrix proj_stack[2];
	struct matrix pos_stack[32];
	struct matrix dir_stack[32];
	struct matrix tex_stack[2];
	struct matrix proj_matrix;
	struct matrix pos_matrix;
	struct matrix dir_matrix;
	struct matrix tex_matrix;
	struct matrix clip_matrix;
	uint8_t matrix_mode;
	uint8_t proj_stack_pos;
	uint8_t pos_stack_pos;
	uint8_t tex_stack_pos;
	struct vec4 position;
	struct vec3 normal;
	struct vec2 texcoord;
	uint32_t polygon_attr;
	uint32_t commit_polygon_attr;
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t primitive;
	uint8_t tmp_vertex;
	uint8_t swap_buffers;
	uint8_t viewport_left;
	uint8_t viewport_right;
	uint8_t viewport_top;
	uint8_t viewport_bottom;
};

struct gpu
{
	struct gpu_eng enga;
	struct gpu_eng engb;
	struct gpu_g3d g3d;
	struct mem *mem;
};

struct gpu *gpu_new(struct mem *mem);
void gpu_del(struct gpu *gpu);

void gpu_draw(struct gpu *gpu, uint8_t y);
void gpu_commit_bgpos(struct gpu *gpu);
void gpu_g3d_draw(struct gpu *gpu);

void gpu_gx_cmd(struct gpu *gpu, uint8_t cmd, uint32_t *params);

#endif
