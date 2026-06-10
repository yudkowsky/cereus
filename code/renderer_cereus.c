#define VK_USE_PLATFORM_WIN32_KHR
#define STB_IMAGE_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION

#include <vulkan/vulkan.h>
#include "stb_image.h"
#include "cgltf.h"
#include "everything.h"

// TEMP: for profiling
#include <windows.h>

typedef struct 
{
    float x, y, z;
    float u, v;
    float nx, ny, nz;
    float r, g, b;
}
Vertex;

typedef struct
{
    uint32 asset_index;
    Vec3 coords;
	Vec3 size;
    Vec4 uv;
    float alpha;
}
Sprite;

typedef struct 
{
	uint32 asset_index;
    Vec3 coords;
    Vec3 scale;
    Vec4 rotation;
    Vec4 uv;
}
Cube;

typedef struct
{
    uint32 model_id;
    Vec3 coords;
    Vec3 scale;
    Vec4 rotation;
    Vec4 color; // used for player color when hit by laser
}
Model;

typedef struct
{
    Vec3 coords;
}
Water;

typedef struct 
{
    Vec3 center;
    float length;
    Vec4 rotation;
    Vec4 color;
    Vec4 start_clip_plane;
    Vec4 end_clip_plane;
}
Laser;

// for instancing of cubes. not required in main build
typedef struct 
{
    float model[16];
    Vec4 uv_rect;
    float padding[4];
}
CubeInstanceData;

// instancing water. this might be the permanent solution here?
typedef struct
{
    float model[16];
}
WaterInstanceData;

typedef struct LaserInstanceData
{
    Vec3 center;
    float length;
    Vec4 rotation;
    Vec4 color;
    Vec4 start_clip_plane;
    Vec4 end_clip_plane;
}
LaserInstanceData;

typedef struct 
{
	VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    char path[256];
}
CachedAsset;

// constant across one view
typedef struct ViewConstants
{
    float view[16];
    float proj[16];
    float view_proj[16];
    float inv_view_proj[16];
    float light_view_proj[16];
    Vec4 camera_position;
    Vec4 light_direction;
    float water_plane_y;
    bool discard_below_water_plane;
    float time;
    float water_tile_length;
    float focal_length;
}
ViewConstants;

#define VIEW_MAIN 0
#define VIEW_REFLECTION 1
#define VIEW_SPRITE 2
#define VIEW_BLOCK_COUNT 3

typedef struct SpritePushConstants
{
    float model[16];
    Vec4 uv_rect;
    Vec4 color;
}
SpritePushConstants;

typedef struct ModelPushConstants
{
    float model[16];
    Vec4 color;
}
ModelPushConstants;

typedef struct OutlinePushConstants
{
    float model[16];
}
OutlinePushConstants;

typedef struct
{
    float texel_width;
    float texel_height;
    float max_depth_difference;
    float outline_radius_px;
} 
WaterlinePushConstants;

typedef struct
{
    int32 texture_size;
    float water_tile_length;
    float wind_direction_x;
    float wind_direction_z;
    float peak_frequency;
    float peak_enhancement;
    float depth;
    float amplitude;
    float gravity;
    uint32 random_seed;
}
FFTSpectrumPushConstants;

typedef struct
{
    int32 texture_size;
    float water_tile_length;
    float gravity;
    float time;
}
FFTEvolvedPushConstants;

typedef struct
{
    int32 texture_size;
    int32 level;
    int32 direction;
}
FFTPassPushConstants;

typedef struct
{
    int32 texture_size;
    float water_tile_length;
}
FFTFinalizePushConstants;

typedef struct
{
    cgltf_data* data;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    uint32 index_count;
}
LoadedModel;

typedef struct
{
    uint32 vertex_offset; // in vertices
    uint32 index_offset; // in indices
    uint32 index_count;
    uint32 _;
}
ModelMeshInfo;

typedef struct VulkanState
{
    // platform and instance
    RendererPlatformHandles platform_handles;
    VkInstance vulkan_instance_handle;
    VkSurfaceKHR surface_handle;
	VkPhysicalDevice physical_device_handle;

    // profiling
    VkQueryPool timestamp_query_pools[3]; // will add more in-flight frames soon, maybe
    uint32 timestamp_query_count;
    uint32 timestamp_query_counts[3];
    float timestamp_period;
    uint64 timestamp_results[3][32];
    bool timestamp_pool_valid[3];
    uint32 timestamp_frame_index;

    // device and queues
    uint32 graphics_family_index;
    uint32 present_family_index;
    VkQueue graphics_queue_handle;
    VkQueue present_queue_handle;
    VkDevice logical_device_handle;

    // swapchain
	VkSwapchainKHR swapchain_handle;
    uint32 swapchain_image_count;
    VkImageView* swapchain_image_views;
	VkFormat swapchain_format;
    VkExtent2D swapchain_extent;

    VkImage* swapchain_images;

    // commands + sync
    VkCommandPool graphics_command_pool_handle;
    VkCommandBuffer* swapchain_command_buffers;
	uint32 frames_in_flight;
	uint32 current_frame;
    VkSemaphore* image_available_semaphores; // semaphore(s) that handle WSI -> graphics. wsi produces swapchain image, graphics queue renders into that image.
    VkSemaphore* render_finished_semaphores; // semaphore(s) that handle graphics -> present. once graphics finishes rendering, graphics sends renders to be presented.
    VkFence* in_flight_fences; 
    VkFence* images_in_flight;

    // composite image (copy of swapchain for tint pass to read)
    VkImage scene_copy_image;
    VkDeviceMemory scene_copy_image_memory;
    VkImageView scene_copy_image_view;
    VkDescriptorSet scene_copy_descriptor_set;

    // scene depth + normal (shared attachments)
    VkImage depth_image;
    VkFormat depth_format;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
    VkImageView depth_sampled_view;
    VkDescriptorSet depth_descriptor_set;

    VkImage normal_image;
    VkDeviceMemory normal_image_memory;
    VkImageView normal_image_view;
    VkDescriptorSet normal_descriptor_set;

    // water depth info for waterline outline
    VkImage water_depth_image;
    VkDeviceMemory water_depth_image_memory;
    VkImageView water_depth_image_view;
    VkDescriptorSet water_depth_descriptor_set;

    // OIT resources for laser rendering
    VkImage oit_head_image;
    VkDeviceMemory oit_head_memory;
    VkImageView oit_head_view;
    VkDescriptorSet oit_head_storage_descriptor_set;
    VkDescriptorSetLayout storage_image_descriptor_set_layout;
    VkDescriptorSetLayout ssbo_descriptor_set_layout;

    // FFT resources for water surface

    // h0 spectrum at startup
    VkImage h0_image;
    VkDeviceMemory h0_image_memory;
    VkImageView h0_image_view;
    VkDescriptorSet h0_descriptor_set;

    // ping ponging buffers for FFT implementation
    VkImage fft_buffer_a_image;
    VkDeviceMemory fft_buffer_a_image_memory;
    VkImageView fft_buffer_a_image_view;
    VkDescriptorSet fft_buffer_a_descriptor_set;
    VkDescriptorSet fft_buffer_a_sampled_descriptor_set;

    VkImage fft_buffer_b_image;
    VkDeviceMemory fft_buffer_b_image_memory;
    VkImageView fft_buffer_b_image_view;
    VkDescriptorSet fft_buffer_b_descriptor_set;
    VkDescriptorSet fft_buffer_b_sampled_descriptor_set;

    // real valued heightfield for water vertex shader TODO: rename water_
    VkImage displacement_image;
    VkDeviceMemory displacement_image_memory;
    VkImageView displacement_image_view;
    VkDescriptorSet displacement_descriptor_set;
    VkDescriptorSet displacement_sampled_descriptor_set;

    // reflections
    VkImage reflection_color_image;
    VkDeviceMemory reflection_color_image_memory;
    VkImageView reflection_color_image_view;
    VkDescriptorSet reflection_descriptor_set;

    VkImage reflection_msaa_color_image;
    VkDeviceMemory reflection_msaa_color_image_memory;
    VkImageView reflection_msaa_color_image_view;

    VkImage reflection_msaa_depth_image;
    VkDeviceMemory reflection_msaa_depth_image_memory;
    VkImageView reflection_msaa_depth_image_view;

    // shadow map for directional light
    VkImage shadow_map_image;
    VkDeviceMemory shadow_map_image_memory;
    VkImageView shadow_map_image_view;
    VkDescriptorSet shadow_map_descriptor_set;

    // RENDER PASSES

    // shadow map pass
    VkRenderPass shadow_render_pass;
    VkFramebuffer shadow_framebuffer;

    // scene pass (cubes, models, select outlines)
    VkRenderPass render_pass_handle;
    VkFramebuffer* swapchain_framebuffers;

    // outline post pass (black outlines on everything)
    VkRenderPass outline_post_render_pass;
    VkFramebuffer* outline_post_framebuffers;
 
    // overlay pass (lasers, which color the outlines, and sprites, which go over the outlines)
    VkRenderPass overlay_render_pass;
    VkFramebuffer* overlay_framebuffers;

    // water pass (also writes depth to a buffer)
    VkRenderPass water_render_pass;
    VkFramebuffer* water_framebuffers;

    // waterline outline
    VkRenderPass waterline_render_pass;
    VkFramebuffer* waterline_framebuffers;

    // reflected scene render pass
    VkRenderPass reflection_render_pass;
    VkFramebuffer reflection_framebuffer; // just 1

    float water_plane_y; // set every frame TODO: this is now hardcoded, stop using this?

    // PIPELINES AND LAYOUTS

    VkPipelineLayout default_graphics_pipeline_layout;

    VkPipeline cube_pipeline;
    VkPipeline cube_reflection_pipeline;
    VkPipelineLayout cube_pipeline_layout; 

    VkPipeline model_pipeline;
    VkPipeline model_reflection_pipeline;
    VkPipelineLayout model_pipeline_layout;

    VkPipeline outline_post_pipeline;
    VkPipelineLayout outline_post_pipeline_layout;

    VkPipeline water_pipeline;
    VkPipelineLayout water_pipeline_layout;

    VkPipeline waterline_pipeline;
    VkPipelineLayout waterline_pipeline_layout;

    VkPipeline editor_outline_pipeline;
    VkPipelineLayout editor_outline_pipeline_layout;

    VkPipeline laser_reflection_pipeline;
    VkPipelineLayout laser_reflection_pipeline_layout;

    VkPipeline laser_pipeline;
    VkPipelineLayout laser_pipeline_layout;

    VkPipeline oit_resolve_pipeline;
    VkPipelineLayout oit_resolve_pipeline_layout;

    VkPipeline sprite_pipeline;
    VkPipelineLayout sprite_pipeline_layout;

    VkPipeline fft_spectrum_pipeline;
    VkPipelineLayout fft_spectrum_pipeline_layout;

    VkPipeline fft_evolved_pipeline;
    VkPipelineLayout fft_evolved_pipeline_layout;

    VkPipeline fft_pass_pipeline;
    VkPipelineLayout fft_pass_pipeline_layout;

    VkPipeline fft_finalize_pipeline;             
    VkPipelineLayout fft_finalize_pipeline_layout;

    VkPipeline shadow_cube_pipeline;
    VkPipeline shadow_model_pipeline;
    VkPipelineLayout shadow_pipeline_layout;

    // shared resources
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[1024];

    // samplers
    VkSampler pixel_art_sampler;
    VkSampler linear_clamp_sampler;
    VkSampler tiling_linear_sampler;
    VkSampler shadow_sampler;

    // asset cache
    CachedAsset asset_cache[256];
    uint32 asset_cache_count;
    int32 atlas_2d_asset_index;
	int32 atlas_font_asset_index;
    int32 atlas_3d_asset_index;
    int32 water_grid_asset_index;
    int32 water_grid_normal_asset_index;

    // shared ubo
    VkBuffer scene_ubo_buffers[2];
    VkDeviceMemory scene_ubo_memories[2];
    void* scene_ubo_mappeds[2];
    uint32 view_constants_stride;
    VkDescriptorSetLayout view_constants_set_layout;
    VkDescriptorSet view_constants_descriptor_sets[2];

    // water paint texture
    WaterPaintTexture* water_paint_texture;

    VkImage paint_image;
    VkDeviceMemory paint_image_memory;
    VkImageView paint_image_view;
    VkDescriptorSet paint_descriptor_set;
    VkSampler paint_sampler;

    VkBuffer paint_staging_buffer;
    VkDeviceMemory paint_staging_memory;
    void* paint_staging_mapped;

    bool paint_image_first_upload;

    // geometry buffers
	VkBuffer sprite_vertex_buffer;
    VkDeviceMemory sprite_vertex_memory;
    VkBuffer sprite_index_buffer;
    VkDeviceMemory sprite_index_memory;
    uint32 sprite_index_count;

    VkBuffer cube_vertex_buffer;
    VkDeviceMemory cube_vertex_memory;
    VkBuffer cube_index_buffer;
    VkDeviceMemory cube_index_memory;
    uint32 cube_index_count;

    // instanced buffers
    VkBuffer cube_instance_buffers[2];
	VkDeviceMemory cube_instance_memories[2];
    void* cube_instance_mappeds[2];
    uint32 cube_instance_capacity;

    VkBuffer water_instance_buffers[2];
    VkDeviceMemory water_instance_memories[2];
    void* water_instance_mappeds[2];
    uint32 water_instance_capacity;

    VkBuffer laser_instance_buffers[2];
    VkDeviceMemory laser_instance_memories[2];
    void* laser_instance_mappeds[2];
    uint32 laser_instance_capacity;

    // models
    LoadedModel loaded_models[64];
    LoadedModel laser_cylinder_model; // TODO: probably index everything into loaded models; figure out what order i want to put stuff in, if can't just take their id
    LoadedModel dummy_cube_model;

    // OIT stuff
    VkBuffer oit_fragment_pool;
    VkDeviceMemory oit_fragment_pool_memory;
    VkDescriptorSet oit_fragment_pool_descriptor_set;

    VkBuffer oit_counter_buffer;
    VkDeviceMemory oit_counter_memory;
    VkDescriptorSet oit_counter_descriptor_set;
}
VulkanState;

#define U0 (0.0f)
#define U1 (1.0f/3.0f)
#define U2 (2.0f/3.0f)
#define U3 (1.0f)

#define V0 (0.0f)
#define V1 (0.5f)
#define V2 (1.0f)

const Vertex SPRITE_VERTICES[] =
{
    { -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,  0,0,1 },
    {  0.5f, -0.5f, 0.0f,  1.0f, 1.0f,  0,0,1 },
    {  0.5f,  0.5f, 0.0f,  1.0f, 0.0f,  0,0,1 },
    { -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,  0,0,1 },
};

const uint32 SPRITE_INDICES[6] =
{
    0, 1, 2,
    0, 2, 3
};

const Vertex CUBE_VERTICES[] = 
{
    { -0.5f, -0.5f,  0.5f,  U0, V1,  0,0,1 },
    {  0.5f, -0.5f,  0.5f,  U1, V1,  0,0,1 },
    {  0.5f,  0.5f,  0.5f,  U1, V0,  0,0,1 },
    { -0.5f,  0.5f,  0.5f,  U0, V0,  0,0,1 },

    {  0.5f, -0.5f, -0.5f,  U0, V2,  0,0,-1 },
    { -0.5f, -0.5f, -0.5f,  U1, V2,  0,0,-1 },
    { -0.5f,  0.5f, -0.5f,  U1, V1,  0,0,-1 },
    {  0.5f,  0.5f, -0.5f,  U0, V1,  0,0,-1 },

    { -0.5f, -0.5f, -0.5f,  U1, V1,  -1,0,0 },
    { -0.5f, -0.5f,  0.5f,  U2, V1,  -1,0,0 },
    { -0.5f,  0.5f,  0.5f,  U2, V0,  -1,0,0 },
    { -0.5f,  0.5f, -0.5f,  U1, V0,  -1,0,0 },

    {  0.5f, -0.5f,  0.5f,  U1, V2,  1,0,0 },
    {  0.5f, -0.5f, -0.5f,  U2, V2,  1,0,0 },
    {  0.5f,  0.5f, -0.5f,  U2, V1,  1,0,0 },
    {  0.5f,  0.5f,  0.5f,  U1, V1,  1,0,0 },

    { -0.5f,  0.5f,  0.5f,  U2, V0,  0,1,0 },
    {  0.5f,  0.5f,  0.5f,  U3, V0,  0,1,0 },
    {  0.5f,  0.5f, -0.5f,  U3, V1,  0,1,0 },
    { -0.5f,  0.5f, -0.5f,  U2, V1,  0,1,0 },

    {  0.5f, -0.5f,  0.5f,  U2, V1,  0,-1,0 },
    { -0.5f, -0.5f,  0.5f,  U3, V1,  0,-1,0 },
    { -0.5f, -0.5f, -0.5f,  U3, V2,  0,-1,0 },
    {  0.5f, -0.5f, -0.5f,  U2, V2,  0,-1,0 },
};

const uint32 CUBE_INDICES[36] = 
{
    0, 1, 2,  0, 2, 3,
    4, 5, 6,  4, 6, 7,
    8, 9,10,  8,10,11,
   12,13,14, 12,14,15,
   16,17,18, 16,18,19,
   20,21,22, 20,22,23
};

const uint32 CUBE_INSTANCE_CAPACITY = 8192;
const uint32 WATER_INSTANCE_CAPACITY = 8192;
const uint32 LASER_INSTANCE_CAPACITY = 1024;

const int32 REFLECTION_DOWNSCALE = 2;

const uint32 SHADOW_MAP_RESOLUTION = 4096;

// TODO: set these in loadAsset where stb_image gives me width / height. store in CachedAsset.
const int32 ATLAS_2D_WIDTH = 128;
const int32 ATLAS_2D_HEIGHT = 128;
const int32 ATLAS_FONT_WIDTH = 120;
const int32 ATLAS_FONT_HEIGHT = 180;
const int32 ATLAS_3D_WIDTH = 480;
const int32 ATLAS_3D_HEIGHT = 320;

const char* ATLAS_2D_PATH 	       = "data/assets/sprites/atlas-2d.png";
const char* ATLAS_FONT_PATH        = "data/assets/sprites/atlas-font.png";
const char* ATLAS_3D_PATH 	       = "data/assets/sprites/atlas-3d.png";
const char* WATER_GRID_PATH        = "data/assets/maps/water-grid/water-grid.dds";
const char* WATER_GRID_NORMAL_PATH = "data/assets/maps/water-grid/water-grid-normal.dds";

VulkanState vulkan_state;
DisplayInfo vulkan_display = {0};
Camera vulkan_camera = {0};
ShaderMode shader_mode = SHADER_MODE_DEFAULT;

// water
float water_time = 0.0f;
const float water_tile_length = 10.0f;
const float water_amplitude = 2e-4f;

// outlines
const float depth_threshold = 1.5f;
const float normal_threshold = 0.33f;

Cube cube_instances[8192];
uint32 cube_instance_count = 0;

Model model_instances[1024];
uint32 model_instance_count = 0;

Water water_instances[8192];
uint32 water_instance_count = 0;

Model model_editor_outline_instances[8192];
uint32 model_editor_outline_instance_count = 0;

Laser laser_instances[1024];
uint32 laser_instance_count = 0;

Sprite sprite_instances[8192];
uint32 sprite_instance_count = 0;

void mat4Identity(float matrix[16]) 
{
	memset(matrix, 0, sizeof(float) * 16);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

// can use an actually optimised matrix mult algorithm here. 
void mat4Multiply(float output_matrix[16], float a[16], float b[16])
{
    float temporary_matrix[16]; // required if output_matrix = a or b
	for (int column = 0; column < 4; column++)
    {
        for (int row = 0; row < 4; row++)
        {
            temporary_matrix[column*4 + row] = (a[row] * b[column*4]) + (a[4 + row] * b[column*4 + 1]) + (a[8 + row] * b[column*4 + 2]) + (a[12 + row] * b[column*4 + 3]);
        }
    }
    memcpy(output_matrix, temporary_matrix, sizeof(temporary_matrix));
}

void mat4Inverse(float out[16], float m[16])
{
    float inv[16];
    
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

    inv[2]  =  m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];

    inv[3]  = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9]  + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10] - m[0]*m[6]*m[9]  - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float determinant = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (determinant == 0.0f)
    { 
        memcpy(out, m, 64); 
        return; 
    }

    determinant = 1.0f / determinant;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * determinant;
}

// TODO: clean these mat4Build... functions up a bit
void mat4BuildTranslation(float output_matrix[16], Vec3 translation) 
{
    mat4Identity(output_matrix);
	output_matrix[12] = translation.x;
    output_matrix[13] = translation.y;
    output_matrix[14] = translation.z;
}

void mat4BuildScale(float output_matrix[16], Vec3 scale)
{
    memset(output_matrix, 0, sizeof(float) * 16);
	output_matrix[0]  = scale.x;
    output_matrix[5]  = scale.y;
    output_matrix[10] = scale.z;
    output_matrix[15] = 1.0f;
}

void mat4BuildRotation(float output_matrix[16], Vec4 quaternion)
{
    float x = quaternion.x, y = quaternion.y, z = quaternion.z, w = quaternion.w;
    float length_squared = x*x + y*y + z*z + w*w;
    if (length_squared < 1e-8f)
    {
        mat4Identity(output_matrix);
        return;
    }
    float inv_length = 1.0f / sqrtf(length_squared);
	x *= inv_length;
	y *= inv_length;
	z *= inv_length;
	w *= inv_length;

    output_matrix[0]  = 1.0f - 2.0f*(y*y + z*z);
    output_matrix[1]  = 2.0f*(x*y + w*z);
    output_matrix[2]  = 2.0f*(x*z - w*y);
    output_matrix[3]  = 0.0f;
    output_matrix[4]  = 2.0f*(x*y - w*z);
    output_matrix[5]  = 1.0f - 2.0f*(x*x + z*z);
    output_matrix[6]  = 2.0f*(y*z + w*x);
    output_matrix[7]  = 0.0f;
    output_matrix[8]  = 2.0f*(x*z + w*y);
    output_matrix[9]  = 2.0f*(y*z - w*x);
    output_matrix[10] = 1.0f - 2.0f*(x*x + y*y);
    output_matrix[11] = 0.0f;
    output_matrix[12] = 0.0f;
    output_matrix[13] = 0.0f;
    output_matrix[14] = 0.0f;
    output_matrix[15] = 1.0f;
}

void mat4BuildTRS(float output_matrix[16], Vec3 translation, Vec4 quaternion, Vec3 scale)
{
    float translation_matrix[16], rotation_matrix[16], scale_matrix[16], translation_rotation_matrix[16];
    mat4BuildTranslation(translation_matrix, translation);
    mat4BuildRotation(rotation_matrix, quaternion);
	mat4BuildScale(scale_matrix, scale);
    mat4Multiply(translation_rotation_matrix, translation_matrix, rotation_matrix);
    mat4Multiply(output_matrix, translation_rotation_matrix, scale_matrix);
}

// assumes identity rotation, no scaling
void mat4BuildBasicTRS(float output_matrix[16], Vec3 translation)
{
    memset(output_matrix, 0, 64);
    output_matrix[0] = 1.0f;
    output_matrix[5] = 1.0f;
    output_matrix[10] = 1.0f;
    output_matrix[15] = 1.0f;
    output_matrix[12] = translation.x;
    output_matrix[13] = translation.y;
    output_matrix[14] = translation.z;
}

void mat4BuildViewFromQuat(float output_matrix[16], Vec3 coords, Vec4 quaternion)
{
    mat4Identity(output_matrix);

    float length_squared = quaternion.x*quaternion.x + quaternion.y*quaternion.y + quaternion.z*quaternion.z + quaternion.w*quaternion.w;
    if (length_squared < 1e-8f) 
    { 
        output_matrix[12] = -coords.x; 
        output_matrix[13] = -coords.y; 
        output_matrix[14] = -coords.z;
        return;
    }
    float inverse_length = 1.0f / sqrtf(length_squared);
    float x = quaternion.x*inverse_length, y = quaternion.y*inverse_length, z = quaternion.z*inverse_length, w = quaternion.w*inverse_length;

    // basis vectors
    float right_x = 1.0f - (2.0f*y*y + 2.0f*z*z);
    float right_y = 2.0f*x*y + 2.0f*w*z;
    float right_z = 2.0f*x*z - 2.0f*w*y;

    float up_x = 2.0f*x*y - 2.0f*w*z;
    float up_y = 1.0f - (2.0f*x*x + 2.0f*z*z);
    float up_z = 2.0f*y*z + 2.0f*w*x;

    float forward_x = 2.0f*x*z + 2.0f*w*y;
    float forward_y = 2.0f*y*z - 2.0f*w*x;
    float forward_z = 1.0f - (2.0f*x*x + 2.0f*y*y);

    output_matrix[0] =  right_x;  
    output_matrix[1] =  up_x;  
    output_matrix[2] =  forward_x;  

    output_matrix[4] =  right_y;  
    output_matrix[5] =  up_y;  
    output_matrix[6] =  forward_y;  

    output_matrix[8] =  right_z;  
    output_matrix[9] =  up_z;  
    output_matrix[10] = forward_z;

    output_matrix[12]= -(right_x*coords.x + right_y*coords.y + right_z*coords.z);
    output_matrix[13]= -(up_x*coords.x + up_y*coords.y + up_z*coords.z);
    output_matrix[14]= -(forward_x*coords.x + forward_y*coords.y + forward_z*coords.z);
}

void mat4BuildReflectedView(float output_matrix[16], Vec3 coords, Vec4 quaternion, float plane_y)
{
    // TODO: just do inline
    float view[16];
    mat4BuildViewFromQuat(view, coords, quaternion);

    float mirror[16];
    mat4Identity(mirror);
    mirror[5]  = -1.0f;
    mirror[13] = 2.0f * plane_y;

    mat4Multiply(output_matrix, view, mirror);}

void mat4BuildOrtho(float output_matrix[16], float left, float right, float bottom, float top, float z_near, float z_far)
{
    for (int i = 0; i < 16; i++) output_matrix[i] = 0.0f;

    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = z_far - z_near;

    output_matrix[0]  =  2.0f / rl;
    output_matrix[5]  =  2.0f / tb;
    output_matrix[10] =  1.0f / fn;

    output_matrix[12] = -(right + left) / rl;
    output_matrix[13] = -(top + bottom) / tb;
    output_matrix[14] = -z_near         / fn;

    output_matrix[15] = 1.0f;
}

// right handed, zero to one depth
void mat4BuildPerspective(float output_matrix[16], float fov_y_radians, float aspect, float z_near, float z_far) 
{
	float cotan = 1.0f / tanf(0.5f * fov_y_radians);
    memset(output_matrix, 0, sizeof(float) * 16);

    output_matrix[0] = cotan / aspect;
    output_matrix[5] = cotan;

	output_matrix[10] = z_far / (z_near - z_far);
	output_matrix[11] = -1.0f;
	output_matrix[14] = (z_near * z_far) / (z_near - z_far);
}

void mat4BuildDirectionalLight(float output_matrix[16], Vec3 light_direction, Vec3 coverage_center, float coverage_radius)
{
    // TODO: set up some sort of maths include with functions from game layer
    float direction_length = sqrtf(light_direction.x*light_direction.x + light_direction.y*light_direction.y + light_direction.z*light_direction.z);
    Vec3 forward = { light_direction.x/direction_length, light_direction.y/direction_length, light_direction.z/direction_length };
    Vec3 up_reference = { 0.0f, 1.0f, 0.0f };

    // fabricate viewpoint by stepping back from coverage center against light dir
    Vec3 eye =
    {
        coverage_center.x - forward.x * coverage_radius,
        coverage_center.y - forward.y * coverage_radius,
        coverage_center.z - forward.z * coverage_radius,
    };

    Vec3 right = { 
        (forward.y*up_reference.z - forward.z*up_reference.y),
        (forward.z*up_reference.x - forward.x*up_reference.z),
        (forward.x*up_reference.y - forward.y*up_reference.x)
    };
    float right_length = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
    right.x /= right_length;
    right.y /= right_length;
    right.z /= right_length;

    Vec3 true_up =
    {
        right.y*forward.z - right.z*forward.y,
        right.z*forward.x - right.x*forward.z,
        right.x*forward.y - right.y*forward.x
    };

    // rotate world into light space, slide eye to origin
    float light_view[16];
    mat4Identity(light_view);
    light_view[0] = right.x;    light_view[4] = right.y;    light_view[8]  = right.z;
    light_view[1] = true_up.x;  light_view[5] = true_up.y;  light_view[9]  = true_up.z;
    light_view[2] = forward.x;  light_view[6] = forward.y;  light_view[10] = forward.z;
    light_view[12] = -(right.x*eye.x   + right.y*eye.y   + right.z*eye.z);
    light_view[13] = -(true_up.x*eye.x + true_up.y*eye.y + true_up.z*eye.z);
    light_view[14] = -(forward.x*eye.x + forward.y*eye.y + forward.z*eye.z);

    // ortho box sized to the coverage region
    float light_projection[16];
    mat4BuildOrtho(light_projection, -coverage_radius, coverage_radius, -coverage_radius, coverage_radius, 0.0f, 2.0f * coverage_radius);

    mat4Multiply(output_matrix, light_projection, light_view);
}

int32 reflectionExtent(uint32 full)
{
    uint32 length = (full + REFLECTION_DOWNSCALE - 1) / REFLECTION_DOWNSCALE;
    return length == 0 ? 1 : length;
}

uint32 findMemoryType(uint32 type_bits, VkMemoryPropertyFlags property_flags)
{
    VkPhysicalDeviceMemoryProperties memory_properties = {0};
    vkGetPhysicalDeviceMemoryProperties(vulkan_state.physical_device_handle, &memory_properties);
    for (uint32 memory_type_count_increment = 0; memory_type_count_increment < memory_properties.memoryTypeCount; memory_type_count_increment++)
    {
        bool is_compatible = (type_bits & (1u << memory_type_count_increment)) != 0;
        bool has_properties = (memory_properties.memoryTypes[memory_type_count_increment].propertyFlags & property_flags) == property_flags;
        if (is_compatible && has_properties) return memory_type_count_increment;
    }
    return UINT32_MAX;
}

void imageBarrier(VkCommandBuffer command_buffer, VkImage image,
    VkImageLayout old_layout, VkImageLayout new_layout,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
    VkAccessFlags src_access, VkAccessFlags dst_access,
    VkImageAspectFlags aspect_mask)
{
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;

    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, 0, 0, 0, 1, &barrier);
}

void memoryBarrier(VkCommandBuffer command_buffer,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
    VkAccessFlags src_access, VkAccessFlags dst_access)
{
    VkMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;

    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 1, &barrier, 0, 0, 0, 0);
}

bool readEntireFile(char* path, void** out_data, size_t* out_size)
{
	FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
   	long end_position = ftell(file);
    fseek(file, 0, SEEK_SET);
	size_t file_size_bytes = (size_t)end_position;
	void* file_bytes = malloc(file_size_bytes);
	if (!file_bytes)
    {
        fclose(file);
        return false;
    }
    size_t bytes_read = fread(file_bytes, 1, file_size_bytes, file);
	if (bytes_read != file_size_bytes)
    {
        fclose(file);
        free(file_bytes);
        return false;
    }
	fclose(file);
    *out_data = file_bytes;
    *out_size = file_size_bytes;
    return true;
}

bool spriteIsFont(SpriteId id)
{
    return (id >= SPRITE_2D_FONT_SPACE && id <= SPRITE_2D_FONT_LAST);
}

int32 spriteIndexInAtlas(SpriteId id, AssetType type)
{
    if (type == SPRITE_2D)
    {
        if (spriteIsFont(id)) return (int32)id - (int32)SPRITE_2D_FONT_SPACE;
        else return (int32)id;
    }
    else
    {
        return (int32)id - (int32)(SPRITE_2D_COUNT);
    }
}

void atlasCellSize(SpriteId id, AssetType type, int32* cell_width, int32* cell_height)
{
    if (type == SPRITE_2D)
    {
        if (spriteIsFont(id))
        {
            *cell_width = 6;
            *cell_height = 10;
        }
        else
        {
            *cell_width = 16;
            *cell_height = 16;
        }
        return;
    }
    if (type == CUBE_3D)
    {
        *cell_width = 48;
        *cell_height = 32;
        return;
    }
}

Vec4 spriteUV(SpriteId id, AssetType type, int32 atlas_width, int32 atlas_height)
{
    int32 cell_width = 0, cell_height = 0;
    atlasCellSize(id, type, &cell_width, &cell_height);

    int32 per_row = atlas_width / cell_width;
    int32 index = spriteIndexInAtlas(id, type);

    int32 x = (index % per_row) * cell_width;
    int32 y = (index / per_row) * cell_height;

    float u0 = (float)x / (float)atlas_width;
    float v0 = (float)y / (float)atlas_height;
    float u1 = (float)(x + cell_width) / (float)atlas_width;
    float v1 = (float)(y + cell_height) / (float)atlas_height;

    return (Vec4){u0,v0,u1,v1};
}

int32 loadAsset(char* path, VkFormat format)
{
    int width, height, channels;
    uint8* pixels = (uint8*)stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) return -1;

    VkDeviceSize image_size_bytes = width * height * 4;
    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = image_size_bytes;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_info, 0, &staging_buffer);

    VkMemoryRequirements cpu_memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, staging_buffer, &cpu_memory_requirements);

    VkMemoryAllocateInfo cpu_memory_allocation_info = {0};
    cpu_memory_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    cpu_memory_allocation_info.allocationSize = cpu_memory_requirements.size;
    cpu_memory_allocation_info.memoryTypeIndex = findMemoryType(cpu_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(vulkan_state.logical_device_handle, &cpu_memory_allocation_info, 0, &staging_buffer_memory);
    vkBindBufferMemory(vulkan_state.logical_device_handle, staging_buffer, staging_buffer_memory, 0);

    void* gpu_memory_pointer; // starts as CPU pointer
    vkMapMemory(vulkan_state.logical_device_handle, staging_buffer_memory, 0, image_size_bytes, 0, &gpu_memory_pointer); // data now points to GPU-visible memory
    memcpy(gpu_memory_pointer, pixels, (size_t)image_size_bytes); // writes from pixels to data
    vkUnmapMemory(vulkan_state.logical_device_handle, staging_buffer_memory); // removes CPU access

    stbi_image_free(pixels);

    VkImage texture_image;
    VkDeviceMemory texture_image_memory;

    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1; 
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    vkCreateImage(vulkan_state.logical_device_handle, &image_info, 0, &texture_image);

	VkMemoryRequirements gpu_memory_requirements;
    vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, texture_image, &gpu_memory_requirements);

    VkMemoryAllocateInfo gpu_memory_allocation_info = {0};
    gpu_memory_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    gpu_memory_allocation_info.allocationSize = gpu_memory_requirements.size;
	gpu_memory_allocation_info.memoryTypeIndex = findMemoryType(gpu_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(vulkan_state.logical_device_handle, &gpu_memory_allocation_info, 0, &texture_image_memory);
    vkBindImageMemory(vulkan_state.logical_device_handle, texture_image, texture_image_memory, 0);

    VkCommandBufferAllocateInfo command_buffer_allocation_info = {0};
    command_buffer_allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocation_info.commandPool = vulkan_state.graphics_command_pool_handle;
    command_buffer_allocation_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(vulkan_state.logical_device_handle, &command_buffer_allocation_info, &command_buffer);

    VkCommandBufferBeginInfo command_buffer_begin_info = {0};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    // transition image to transfer
    imageBarrier(command_buffer, texture_image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    // copy from staging buffer to image
    VkBufferImageCopy copy_region = {0};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = (VkOffset3D){ 0, 0, 0 }; // 3rd dimension is depth
    copy_region.imageExtent = (VkExtent3D){ width, height, 1};

    vkCmdCopyBufferToImage(command_buffer, staging_buffer, texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

    imageBarrier(command_buffer, texture_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(command_buffer);

    // submit and execute commands
	VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(vulkan_state.graphics_queue_handle, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkan_state.graphics_queue_handle); // blocks until the GPU finishes 

    vkFreeCommandBuffers(vulkan_state.logical_device_handle, vulkan_state.graphics_command_pool_handle, 1, &command_buffer);
    vkDestroyBuffer(vulkan_state.logical_device_handle, staging_buffer, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, staging_buffer_memory, 0);

	VkImageView texture_image_view;

    VkImageViewCreateInfo image_view_info = {0};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.image = texture_image;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = format;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;

    vkCreateImageView(vulkan_state.logical_device_handle, &image_view_info, 0, &texture_image_view);

    // depth descriptor set update
    VkDescriptorImageInfo descriptor_image_info = {0};
    descriptor_image_info.sampler = vulkan_state.pixel_art_sampler;
    descriptor_image_info.imageView = texture_image_view;
    descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorSetAllocateInfo descriptor_set_alloc = {0};
    descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
    descriptor_set_alloc.descriptorSetCount = 1;
    descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;

	vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &descriptor_set_alloc, &vulkan_state.descriptor_sets[vulkan_state.asset_cache_count]);

	VkWriteDescriptorSet descriptor_set_write = {0};
    descriptor_set_write.sType= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_set_write.dstSet = vulkan_state.descriptor_sets[vulkan_state.asset_cache_count];
	descriptor_set_write.dstBinding = 0;
    descriptor_set_write.descriptorCount = 1;
	descriptor_set_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_set_write.pImageInfo = &descriptor_image_info;

    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &descriptor_set_write, 0, 0);

    vulkan_state.asset_cache[vulkan_state.asset_cache_count].image = texture_image;
    vulkan_state.asset_cache[vulkan_state.asset_cache_count].memory = texture_image_memory;
    vulkan_state.asset_cache[vulkan_state.asset_cache_count].view = texture_image_view;
    strcpy(vulkan_state.asset_cache[vulkan_state.asset_cache_count].path, path);

    vulkan_state.asset_cache_count++;

    return (int32)(vulkan_state.asset_cache_count - 1);
}

// TODO: why do i have this as a separate function?
int32 getOrLoadAsset(char* path, VkFormat format)
{
    // check if already loaded
    for (uint32 asset_cache_index = 0; asset_cache_index < vulkan_state.asset_cache_count; asset_cache_index++)
    {
        if (strcmp(vulkan_state.asset_cache[asset_cache_index].path, path) == 0) return (int32)asset_cache_index;
    }
    return loadAsset(path, format);
}

// load BC4/BC5 DDS array texture TODO: various cleanups / formatting stuff (and that imageBarrier thing)
int32 loadDdsArray(char* path)
{
    void*  file_data = 0;
    size_t file_size = 0;
    if (!readEntireFile(path, &file_data, &file_size)) return -1;

    uint8* bytes = (uint8*)file_data;
    if (memcmp(bytes, "DDS ", 4) != 0) { free(file_data); return -1; }

    // DDS_HEADER fields are at fixed offsets
    uint32 height = *(uint32*)(bytes + 12);
    uint32 width  = *(uint32*)(bytes + 16);
    uint32 levels = *(uint32*)(bytes + 28);
    // DX10 header starts at byte 128
    uint32 dxgi   = *(uint32*)(bytes + 128);
    uint32 layers = *(uint32*)(bytes + 140);
    uint8* texels = bytes + 148;

    VkFormat format;
    uint32 block_bytes;
    if (dxgi == 80) 
    {
        format = VK_FORMAT_BC4_UNORM_BLOCK; 
        block_bytes = 8;
    }
    else if (dxgi == 83)
    { 
        format = VK_FORMAT_BC5_UNORM_BLOCK;
        block_bytes = 16;
    }
    else
    {
        free(file_data);
        return -1;
    }

    // precompute level sizes and get one slice's total size
    uint32 level_size[16] = {0};
    uint32 slice_size = 0;
    for (uint32 level = 0; level < levels; level++)
    {
        uint32 lw = width  >> level; if (lw == 0) lw = 1;
        uint32 lh = height >> level; if (lh == 0) lh = 1;
        uint32 bx = (lw + 3) / 4;
        uint32 by = (lh + 3) / 4;
        level_size[level] = bx * by * block_bytes;
        slice_size += level_size[level];
    }
    VkDeviceSize payload_size = (VkDeviceSize)slice_size * layers;

    // staging buffer
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = payload_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_info, 0, &staging);

    VkMemoryRequirements memory_requirements = {0};
    vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, staging, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(vulkan_state.logical_device_handle, &alloc_info, 0, &staging_memory);
    vkBindBufferMemory(vulkan_state.logical_device_handle, staging, staging_memory, 0);

    void* mapped = 0;
    vkMapMemory(vulkan_state.logical_device_handle, staging_memory, 0, payload_size, 0, &mapped);
    memcpy(mapped, texels, (size_t)payload_size);
    vkUnmapMemory(vulkan_state.logical_device_handle, staging_memory);

    // device local image
    VkImage texture_image = VK_NULL_HANDLE;
    VkDeviceMemory texture_image_memory = VK_NULL_HANDLE;

    VkImageCreateInfo image_info = {0};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent.width  = width;
    image_info.extent.height = height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = levels;
    image_info.arrayLayers   = layers;
    image_info.format        = format;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(vulkan_state.logical_device_handle, &image_info, 0, &texture_image);

    vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, texture_image, &memory_requirements);
    alloc_info.allocationSize  = memory_requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(vulkan_state.logical_device_handle, &alloc_info, 0, &texture_image_memory);
    vkBindImageMemory(vulkan_state.logical_device_handle, texture_image, texture_image_memory, 0);

    // copy regions
    VkBufferImageCopy* regions = malloc(sizeof(VkBufferImageCopy) * levels * layers);
    uint32 region_count = 0;
    VkDeviceSize offset = 0;
    for (uint32 layer = 0; layer < layers; layer++)
    {
        for (uint32 level = 0; level < levels; level++)
        {
            uint32 lw = width  >> level; 
            if (lw == 0) lw = 1;
            uint32 lh = height >> level; 
            if (lh == 0) lh = 1;

            VkBufferImageCopy* region = &regions[region_count++];
            *region = (VkBufferImageCopy){0};
            region->bufferOffset = offset;
            region->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region->imageSubresource.mipLevel = level;
            region->imageSubresource.baseArrayLayer = layer;
            region->imageSubresource.layerCount     = 1;
            region->imageExtent.width  = lw;
            region->imageExtent.height = lh;
            region->imageExtent.depth  = 1;
            offset += level_size[level];
        }
    }

    // submit
    VkCommandBufferAllocateInfo cb_alloc = {0};
    cb_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_alloc.commandPool = vulkan_state.graphics_command_pool_handle;
    cb_alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(vulkan_state.logical_device_handle, &cb_alloc, &cmd);

    VkCommandBufferBeginInfo cb_begin = {0};
    cb_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cb_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cb_begin);

    // full range barrier TODO: imageBarrier here, need to change that function to allow diff level
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = texture_image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = levels;
    barrier.subresourceRange.layerCount = layers;

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

    vkCmdCopyBufferToImage(cmd, staging, texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region_count, regions);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {0};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(vulkan_state.graphics_queue_handle, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkan_state.graphics_queue_handle);

    vkFreeCommandBuffers(vulkan_state.logical_device_handle, vulkan_state.graphics_command_pool_handle, 1, &cmd);
    vkDestroyBuffer(vulkan_state.logical_device_handle, staging, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, staging_memory, 0);
    free(regions);
    free(file_data);

    // array view
    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = levels;
    view_info.subresourceRange.layerCount = layers;
    VkImageView texture_image_view = VK_NULL_HANDLE;
    vkCreateImageView(vulkan_state.logical_device_handle, &view_info, 0, &texture_image_view);

    // descriptor
    VkDescriptorImageInfo image_desc = {0};
    image_desc.sampler = vulkan_state.tiling_linear_sampler;
    image_desc.imageView = texture_image_view;
    image_desc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorSetAllocateInfo ds_alloc = {0};
    ds_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ds_alloc.descriptorPool = vulkan_state.descriptor_pool;
    ds_alloc.descriptorSetCount = 1;
    ds_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &ds_alloc, &vulkan_state.descriptor_sets[vulkan_state.asset_cache_count]);

    VkWriteDescriptorSet write = {0};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = vulkan_state.descriptor_sets[vulkan_state.asset_cache_count];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_desc;
    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);

    // cache bookkeeping
    vulkan_state.asset_cache[vulkan_state.asset_cache_count].image  = texture_image;
    vulkan_state.asset_cache[vulkan_state.asset_cache_count].memory = texture_image_memory;
    vulkan_state.asset_cache[vulkan_state.asset_cache_count].view   = texture_image_view;
    strcpy(vulkan_state.asset_cache[vulkan_state.asset_cache_count].path, path);

    vulkan_state.asset_cache_count++;
    return (int32)(vulkan_state.asset_cache_count - 1);
}

void uploadBufferToLocalDevice(void* source, VkDeviceSize size, VkBufferUsageFlags final_usage, VkBuffer* out_buffer, VkDeviceMemory* out_memory)
{
    // create staging buffer
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_info, 0, &staging_buffer);

    VkMemoryRequirements memory_requirements = {0};
    vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, staging_buffer, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(vulkan_state.logical_device_handle, &alloc_info, 0, &staging_memory);
    vkBindBufferMemory(vulkan_state.logical_device_handle, staging_buffer, staging_memory, 0);

    // copy CPU data into staging memory
    void* mapped = 0;
    vkMapMemory(vulkan_state.logical_device_handle, staging_memory, 0, size, 0, &mapped);
    memcpy(mapped, source, (size_t)size);
    vkUnmapMemory(vulkan_state.logical_device_handle, staging_memory);

    // create device-local buffer
    VkBuffer device_buffer = VK_NULL_HANDLE;
    VkDeviceMemory device_memory = VK_NULL_HANDLE;

    buffer_info = (VkBufferCreateInfo){0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | final_usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_info, 0, &device_buffer);

    vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, device_buffer, &memory_requirements);

    alloc_info = (VkMemoryAllocateInfo){0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(vulkan_state.logical_device_handle, &alloc_info, 0, &device_memory);
    vkBindBufferMemory(vulkan_state.logical_device_handle, device_buffer, device_memory, 0);

    // record + submit copy command
    VkCommandBufferAllocateInfo cb_alloc = {0};
    cb_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_alloc.commandPool = vulkan_state.graphics_command_pool_handle;
    cb_alloc.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(vulkan_state.logical_device_handle, &cb_alloc, &command_buffer);

    VkCommandBufferBeginInfo cb_begin = {0};
    cb_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cb_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer, &cb_begin);

    VkBufferCopy copy_region = {0};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = size;

    vkCmdCopyBuffer(command_buffer, staging_buffer, device_buffer, 1, &copy_region);

    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit = {0};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;

    vkQueueSubmit(vulkan_state.graphics_queue_handle, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkan_state.graphics_queue_handle);

    vkFreeCommandBuffers(vulkan_state.logical_device_handle, vulkan_state.graphics_command_pool_handle, 1, &command_buffer);

    // cleanup and return
    vkDestroyBuffer(vulkan_state.logical_device_handle, staging_buffer, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, staging_memory, 0);

    *out_buffer = device_buffer;
    *out_memory = device_memory;
}

void createInstanceBuffer(VkBuffer* instance_buffer, VkDeviceSize buffer_size, VkDeviceMemory* instance_memory, void** instance_mapped)
{
    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_info, 0, instance_buffer);

    VkMemoryRequirements memory_requirements = {0};
    vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, *instance_buffer, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(vulkan_state.logical_device_handle, &alloc_info, 0, instance_memory);
    vkBindBufferMemory(vulkan_state.logical_device_handle, *instance_buffer, *instance_memory, 0);

    vkMapMemory(vulkan_state.logical_device_handle, *instance_memory, 0, buffer_size, 0, instance_mapped);
    memset(*instance_mapped, 0, (size_t)buffer_size);
}

LoadedModel loadModel(char* path)
{
    LoadedModel result = {0};

    cgltf_options options = {0};
    cgltf_data* data = 0;
    cgltf_result parse_result = cgltf_parse_file(&options, path, &data);
    if (parse_result != cgltf_result_success)
    {
        //LOG("failed to parse gltf file: %s\n", path);
        return result;
    }

    cgltf_result load_result = cgltf_load_buffers(&options, data, path);
    if (load_result != cgltf_result_success)
    {
        //LOG("failed to load gltf buffers: %s\n", path);
        cgltf_free(data);
        return result;
    }

    // first pass: count total verts and indices across all meshes/primitives
    cgltf_size total_verts = 0;
    cgltf_size total_indices = 0;

    for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; mesh_index++)
    {
        for (cgltf_size primitive_index = 0; primitive_index < data->meshes[mesh_index].primitives_count; primitive_index++)
        {
            // every attribute in a primitive describes same set of vertices, and so has the same count; this pass only needs to get the pass for the malloc
            cgltf_primitive* primitive = &data->meshes[mesh_index].primitives[primitive_index];
            if (primitive->attributes_count == 0 || !primitive->indices) continue;
            total_verts += primitive->attributes[0].data->count; 
            total_indices += primitive->indices->count;
        }
    }
    if (total_verts == 0 || total_indices == 0)
    {
        //LOG("no geometry in: %s\n", path);
        cgltf_free(data);
        return result;
    }

    Vertex* vertices = malloc(sizeof(Vertex) * total_verts);
    uint32* indices = malloc(sizeof(uint32) * total_indices);

    cgltf_size vert_offset = 0;
    cgltf_size index_offset = 0;

    // second pass: fill buffers
    for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; mesh_index++)
    {
        for (cgltf_size primitive_index = 0; primitive_index < data->meshes[mesh_index].primitives_count; primitive_index++)
        {
            cgltf_primitive* primitive = &data->meshes[mesh_index].primitives[primitive_index];

            cgltf_accessor* pos_accessor = 0;
            cgltf_accessor* uv_accessor = 0;
            cgltf_accessor* normal_accessor = 0;

            for (cgltf_size attr_index = 0; attr_index < primitive->attributes_count; attr_index ++)
            {
                if 		(primitive->attributes[attr_index].type == cgltf_attribute_type_position) pos_accessor 	  = primitive->attributes[attr_index].data;
                else if (primitive->attributes[attr_index].type == cgltf_attribute_type_texcoord) uv_accessor 	  = primitive->attributes[attr_index].data;
                else if (primitive->attributes[attr_index].type == cgltf_attribute_type_normal)   normal_accessor = primitive->attributes[attr_index].data;
            }
            if (!pos_accessor || !primitive->indices) continue;

    		float base_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            if (primitive->material && primitive->material->has_pbr_metallic_roughness) // name for default gltf material model, for some reason
            {
                base_color[0] = primitive->material->pbr_metallic_roughness.base_color_factor[0];
                base_color[1] = primitive->material->pbr_metallic_roughness.base_color_factor[1];
                base_color[2] = primitive->material->pbr_metallic_roughness.base_color_factor[2];
                base_color[3] = primitive->material->pbr_metallic_roughness.base_color_factor[3];
            }

            cgltf_size vert_count = pos_accessor->count;

            for (cgltf_size vert_index = 0; vert_index < vert_count; vert_index++)
            {
                Vertex* vertex = &vertices[vert_offset + vert_index];
                float pos[3] = {0};
                cgltf_accessor_read_float(pos_accessor, vert_index, pos, 3);
                vertex->x = pos[0];
                vertex->y = pos[1];
                vertex->z = pos[2];

                if (uv_accessor)
                {
                    float uv[2] = {0};
                    cgltf_accessor_read_float(uv_accessor, vert_index, uv, 2);
                    vertex->u = uv[0];
                    vertex->v = uv[1];
                }
                else
                {
                    vertex->u = 0.0f;
                    vertex->v = 0.0f;
                }
                if (normal_accessor)
                {
                    float normals[3] = {0};
                    cgltf_accessor_read_float(normal_accessor, vert_index, normals, 3);
                    vertex->nx = normals[0];
                    vertex->ny = normals[1];
                    vertex->nz = normals[2];
                }
                else
                {
                    vertex->nx = 0.0f;
                    vertex->ny = 1.0f;
                    vertex->nz = 0.0f;
                }

                vertex->r = base_color[0];
                vertex->g = base_color[1];
                vertex->b = base_color[2];
            }

            // merge primitives into big vertex array with offset 
            for (cgltf_size index_index = 0; index_index < primitive->indices->count; index_index++)
            {
                indices[index_offset + index_index] = (uint32)(cgltf_accessor_read_index(primitive->indices, index_index) + vert_offset);
            }
            vert_offset += vert_count;
            index_offset += primitive->indices->count;
        }
    }

    uploadBufferToLocalDevice(vertices, sizeof(Vertex) * total_verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &result.vertex_buffer, &result.vertex_memory);
    uploadBufferToLocalDevice(indices, sizeof(uint32) * total_indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &result.index_buffer, &result.index_memory);

    result.index_count = (uint32)total_indices;
    result.data = data;

    free(vertices);
    free(indices);

    //LOG("loaded model: %s (%u verts, %u indices)\n", path, (uint32)total_verts, (uint32)total_indices);

    return result;
}

void loadAllEntities()
{
    vulkan_state.loaded_models[MODEL_3D_BOX - MODEL_3D_VOID] = loadModel("data/assets/models/rock.glb");
    vulkan_state.loaded_models[MODEL_3D_PLAYER - MODEL_3D_VOID] = loadModel("data/assets/models/player.glb");
	vulkan_state.loaded_models[MODEL_3D_MIRROR 	- MODEL_3D_VOID] = loadModel("data/assets/models/mirror.glb");
    //vulkan_state.loaded_models[MODEL_3D_GLASS - MODEL_3D_VOID] = loadModel("data/assets/models/glass.glb");
    vulkan_state.loaded_models[MODEL_3D_PACK - MODEL_3D_VOID] = loadModel("data/assets/models/pack.glb");
    vulkan_state.loaded_models[MODEL_3D_WIN_BLOCK - MODEL_3D_VOID] = loadModel("data/assets/models/flower.glb");

    vulkan_state.loaded_models[MODEL_3D_WATER - MODEL_3D_VOID] = loadModel("data/assets/models/water.glb");

    vulkan_state.loaded_models[MODEL_3D_SOURCE_RED     - MODEL_3D_VOID] = loadModel("data/assets/models/red-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_BLUE    - MODEL_3D_VOID] = loadModel("data/assets/models/blue-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_MAGENTA - MODEL_3D_VOID] = loadModel("data/assets/models/magenta-source.glb");

    vulkan_state.laser_cylinder_model = loadModel("data/assets/models/laser-cylinder.glb");
    vulkan_state.dummy_cube_model = loadModel("data/assets/models/dummy-cube.glb");
}

VkPipelineShaderStageCreateInfo loadShaderStage(char* path, VkShaderModule* module, VkShaderStageFlagBits stage_bit)
{
    // load module
    void* bytes = 0;
    size_t size = 0;
    if (!readEntireFile(path, &bytes, &size)) return (VkPipelineShaderStageCreateInfo){0};
    VkShaderModuleCreateInfo shader_module_ci = {0};
    shader_module_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_ci.codeSize = size;
    shader_module_ci.pCode = (uint32*)bytes;
    vkCreateShaderModule(vulkan_state.logical_device_handle, &shader_module_ci, 0, module);
    free(bytes);

    // load stage
    VkPipelineShaderStageCreateInfo shader_stage_ci = {0};
    shader_stage_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_ci.stage = stage_bit;
    shader_stage_ci.module = *module;
    shader_stage_ci.pName = "main";
    return shader_stage_ci;
}

void createSwapchainResources(void)
{
    // query current surface state
    VkSurfaceCapabilitiesKHR surface_capabilities = {0};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_state.physical_device_handle, vulkan_state.surface_handle, &surface_capabilities);

    VkExtent2D chosen_extent = surface_capabilities.currentExtent;
    if (chosen_extent.width == UINT32_MAX)
    {
        // TODO(spike): pass client rect from windows layer. actually, display_info should just be the client, not the display.
        RECT window_rect = {0};
        GetClientRect(vulkan_state.platform_handles.window_handle, &window_rect);
        uint32 width =  (uint32)(window_rect.right - window_rect.left);
        uint32 height = (uint32)(window_rect.bottom - window_rect.top);
        if (width  < surface_capabilities.minImageExtent.width)  width  = surface_capabilities.minImageExtent.width;
        if (width  > surface_capabilities.maxImageExtent.width)  width  = surface_capabilities.maxImageExtent.width;
        if (height < surface_capabilities.minImageExtent.height) height = surface_capabilities.minImageExtent.height;
        if (height > surface_capabilities.maxImageExtent.height) height = surface_capabilities.maxImageExtent.height;
        chosen_extent.width = width;
        chosen_extent.height = height;
    }
    vulkan_state.swapchain_extent = chosen_extent;

    // swapchain
    uint32 min_image_count = 3; // almost certainly fine, might not work on some devices later?
    if (surface_capabilities.maxImageCount != 0 && min_image_count > surface_capabilities.maxImageCount) min_image_count = surface_capabilities.maxImageCount;

    uint32 queue_family_indices[2] = { vulkan_state.graphics_family_index, vulkan_state.present_family_index };
    bool same_family = (vulkan_state.graphics_family_index == vulkan_state.present_family_index);

    VkSwapchainKHR old_swapchain = vulkan_state.swapchain_handle;

    VkSwapchainCreateInfoKHR swapchain_ci = {0};
    swapchain_ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_ci.surface = vulkan_state.surface_handle;
    swapchain_ci.minImageCount = min_image_count;
    swapchain_ci.imageFormat = vulkan_state.swapchain_format;
    swapchain_ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_ci.imageExtent = chosen_extent;
    swapchain_ci.imageArrayLayers = 1;
    swapchain_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapchain_ci.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; // TODO: store / handle chosen present mode
    swapchain_ci.preTransform = surface_capabilities.currentTransform;
    swapchain_ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_ci.clipped = VK_TRUE;
    swapchain_ci.oldSwapchain = old_swapchain;

    if (same_family) 
    {
        swapchain_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    else
    {
        swapchain_ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_ci.queueFamilyIndexCount = 2;
        swapchain_ci.pQueueFamilyIndices = queue_family_indices;
    }

    vkCreateSwapchainKHR(vulkan_state.logical_device_handle, &swapchain_ci, 0, &vulkan_state.swapchain_handle);

    if (old_swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(vulkan_state.logical_device_handle, old_swapchain, 0);

    // swapchain image views
    vkGetSwapchainImagesKHR(vulkan_state.logical_device_handle, vulkan_state.swapchain_handle, &vulkan_state.swapchain_image_count, 0);
    vulkan_state.swapchain_images = malloc(sizeof(VkImage) * vulkan_state.swapchain_image_count);
    vkGetSwapchainImagesKHR(vulkan_state.logical_device_handle, vulkan_state.swapchain_handle, &vulkan_state.swapchain_image_count, vulkan_state.swapchain_images);

    vulkan_state.swapchain_image_views = realloc(vulkan_state.swapchain_image_views, sizeof(VkImageView) * vulkan_state.swapchain_image_count);

    {
        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = vulkan_state.swapchain_format;
        view_ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        for (uint32 image_index = 0; image_index < vulkan_state.swapchain_image_count; image_index++)
        {
            view_ci.image = vulkan_state.swapchain_images[image_index];
            vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.swapchain_image_views[image_index]);
        }
    }

    // depth image + view
    {
        VkImageCreateInfo depth_image_ci = {0};
        depth_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depth_image_ci.imageType = VK_IMAGE_TYPE_2D;
        depth_image_ci.extent.width = vulkan_state.swapchain_extent.width;
        depth_image_ci.extent.height = vulkan_state.swapchain_extent.height;
        depth_image_ci.extent.depth = 1;
        depth_image_ci.mipLevels = 1;
        depth_image_ci.arrayLayers = 1;
        depth_image_ci.format = vulkan_state.depth_format;
        depth_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_image_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        depth_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &depth_image_ci, 0, &vulkan_state.depth_image);

        VkMemoryRequirements depth_memory_requirements = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.depth_image, &depth_memory_requirements);

        VkMemoryAllocateInfo depth_alloc = {0};
        depth_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depth_alloc.allocationSize = depth_memory_requirements.size;
        depth_alloc.memoryTypeIndex = findMemoryType(depth_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &depth_alloc, 0, &vulkan_state.depth_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.depth_image, vulkan_state.depth_image_memory, 0);

        VkImageViewCreateInfo depth_view_ci = {0};
        depth_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depth_view_ci.image = vulkan_state.depth_image;
        depth_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depth_view_ci.format = vulkan_state.depth_format;
        depth_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_view_ci.subresourceRange.baseMipLevel = 0;
        depth_view_ci.subresourceRange.levelCount = 1;
        depth_view_ci.subresourceRange.baseArrayLayer = 0;
        depth_view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &depth_view_ci, 0, &vulkan_state.depth_image_view);
    }

    // normal image + view
    {
        VkImageCreateInfo normal_image_ci = {0};
        normal_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        normal_image_ci.imageType = VK_IMAGE_TYPE_2D;
        normal_image_ci.extent.width = vulkan_state.swapchain_extent.width;
        normal_image_ci.extent.height = vulkan_state.swapchain_extent.height;
        normal_image_ci.extent.depth = 1;
        normal_image_ci.mipLevels = 1;
        normal_image_ci.arrayLayers = 1;
        normal_image_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        normal_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        normal_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        normal_image_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        normal_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        normal_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &normal_image_ci, 0, &vulkan_state.normal_image);

        VkMemoryRequirements normal_memory_requirements = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.normal_image, &normal_memory_requirements);

        VkMemoryAllocateInfo normal_alloc = {0};
        normal_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        normal_alloc.allocationSize = normal_memory_requirements.size;
        normal_alloc.memoryTypeIndex = findMemoryType(normal_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &normal_alloc, 0, &vulkan_state.normal_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.normal_image, vulkan_state.normal_image_memory, 0);

        VkImageViewCreateInfo normal_view_ci = {0};
        normal_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        normal_view_ci.image = vulkan_state.normal_image;
        normal_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        normal_view_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        normal_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        normal_view_ci.subresourceRange.baseMipLevel = 0;
        normal_view_ci.subresourceRange.levelCount = 1;
        normal_view_ci.subresourceRange.baseArrayLayer = 0;
        normal_view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &normal_view_ci, 0, &vulkan_state.normal_image_view);
    }

    // scene copy image (copy of swapchain for tint pass to read)
    {
        VkImageCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = vulkan_state.swapchain_extent.width;
        ci.extent.height = vulkan_state.swapchain_extent.height;
        ci.extent.depth = 1;
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = vulkan_state.swapchain_format;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &ci, 0, &vulkan_state.scene_copy_image);

        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.scene_copy_image, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.scene_copy_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.scene_copy_image, vulkan_state.scene_copy_image_memory, 0);

        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.scene_copy_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = vulkan_state.swapchain_format;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.scene_copy_image_view);
    }

    // water depth
    {
        VkImageCreateInfo water_depth_image_ci = {0};
        water_depth_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        water_depth_image_ci.imageType = VK_IMAGE_TYPE_2D;
        water_depth_image_ci.extent.width = vulkan_state.swapchain_extent.width;
        water_depth_image_ci.extent.height = vulkan_state.swapchain_extent.height;
        water_depth_image_ci.extent.depth = 1;
        water_depth_image_ci.mipLevels = 1;
        water_depth_image_ci.arrayLayers = 1;
        water_depth_image_ci.format = VK_FORMAT_R32_SFLOAT;
        water_depth_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        water_depth_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        water_depth_image_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        water_depth_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        water_depth_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &water_depth_image_ci, 0, &vulkan_state.water_depth_image);

        VkMemoryRequirements water_depth_memory_requirements = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.water_depth_image, &water_depth_memory_requirements);

        VkMemoryAllocateInfo water_depth_alloc = {0};
        water_depth_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        water_depth_alloc.allocationSize = water_depth_memory_requirements.size;
        water_depth_alloc.memoryTypeIndex = findMemoryType(water_depth_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &water_depth_alloc, 0, &vulkan_state.water_depth_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.water_depth_image, vulkan_state.water_depth_image_memory, 0);

        VkImageViewCreateInfo water_depth_view_ci = {0};
        water_depth_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        water_depth_view_ci.image = vulkan_state.water_depth_image;
        water_depth_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        water_depth_view_ci.format = VK_FORMAT_R32_SFLOAT;
        water_depth_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        water_depth_view_ci.subresourceRange.baseMipLevel = 0;
        water_depth_view_ci.subresourceRange.levelCount = 1;
        water_depth_view_ci.subresourceRange.baseArrayLayer = 0;
        water_depth_view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &water_depth_view_ci, 0, &vulkan_state.water_depth_image_view);
    }

    // OIT head pointer image
    {
        VkImageCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = vulkan_state.swapchain_extent.width;
        ci.extent.height = vulkan_state.swapchain_extent.height;
        ci.extent.depth = 1;
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = VK_FORMAT_R32_UINT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &ci, 0, &vulkan_state.oit_head_image);

        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.oit_head_image, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.oit_head_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.oit_head_image, vulkan_state.oit_head_memory, 0);

        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.oit_head_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_R32_UINT;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.oit_head_view);
    }

    // OIT fragment pool SSBO
    {
        VkDeviceSize pool_size = (VkDeviceSize)vulkan_state.swapchain_extent.width * vulkan_state.swapchain_extent.height * 8 * 16; // 8 frags per pixel, 16 bytes each (uvec4)

        VkBufferCreateInfo buffer_ci = {0};
        buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_ci.size = pool_size;
        buffer_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_ci, 0, &vulkan_state.oit_fragment_pool);

        VkMemoryRequirements mem_req = {0};
        vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.oit_fragment_pool, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.oit_fragment_pool_memory);
        vkBindBufferMemory(vulkan_state.logical_device_handle, vulkan_state.oit_fragment_pool, vulkan_state.oit_fragment_pool_memory, 0);
    }

    // OIT counter SSBO
    {
        VkBufferCreateInfo buffer_ci = {0};
        buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_ci.size = 4;
        buffer_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_ci, 0, &vulkan_state.oit_counter_buffer);

        VkMemoryRequirements mem_req = {0};
        vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.oit_counter_buffer, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.oit_counter_memory);
        vkBindBufferMemory(vulkan_state.logical_device_handle, vulkan_state.oit_counter_buffer, vulkan_state.oit_counter_memory, 0);
    }

    // reflection color image
    uint32 reflection_width  = reflectionExtent(vulkan_state.swapchain_extent.width);
    uint32 reflection_height = reflectionExtent(vulkan_state.swapchain_extent.height);

    {
        VkImageCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = reflection_width;
        ci.extent.height = reflection_height;
        ci.extent.depth = 1;
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = vulkan_state.swapchain_format;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &ci, 0, &vulkan_state.reflection_color_image);

        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.reflection_color_image, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.reflection_color_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.reflection_color_image, vulkan_state.reflection_color_image_memory, 0);

        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.reflection_color_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = vulkan_state.swapchain_format;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.reflection_color_image_view);
    }

    // TODO: think about adding VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT and allocate from LAZILY ALLOCATED memory type if possible
    // reflection MSAA color image
    {
        VkImageCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = reflection_width;
        ci.extent.height = reflection_height;
        ci.extent.depth = 1;
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = vulkan_state.swapchain_format;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.samples = VK_SAMPLE_COUNT_4_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(vulkan_state.logical_device_handle, &ci, 0, &vulkan_state.reflection_msaa_color_image);

        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_color_image, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.reflection_msaa_color_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_color_image, vulkan_state.reflection_msaa_color_image_memory, 0);

        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.reflection_msaa_color_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = vulkan_state.swapchain_format;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.layerCount = 1;
        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.reflection_msaa_color_image_view);
    }

    // reflection MSAA depth image
    {
        VkImageCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = reflection_width;
        ci.extent.height = reflection_height;
        ci.extent.depth = 1;
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = vulkan_state.depth_format;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ci.samples = VK_SAMPLE_COUNT_4_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(vulkan_state.logical_device_handle, &ci, 0, &vulkan_state.reflection_msaa_depth_image);

        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_depth_image, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.reflection_msaa_depth_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_depth_image, vulkan_state.reflection_msaa_depth_image_memory, 0);

        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.reflection_msaa_depth_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = vulkan_state.depth_format;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.layerCount = 1;
        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.reflection_msaa_depth_image_view);
    }

    // command buffers
    vulkan_state.swapchain_command_buffers = realloc(vulkan_state.swapchain_command_buffers, sizeof(VkCommandBuffer) * vulkan_state.swapchain_image_count);

    VkCommandBufferAllocateInfo command_buffer_alloc = {0};
    command_buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_alloc.commandPool = vulkan_state.graphics_command_pool_handle;
    command_buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_alloc.commandBufferCount = vulkan_state.swapchain_image_count;

    vkAllocateCommandBuffers(vulkan_state.logical_device_handle, &command_buffer_alloc, vulkan_state.swapchain_command_buffers);

    // per image fence tracking
    free(vulkan_state.images_in_flight);
    vulkan_state.images_in_flight = calloc(vulkan_state.swapchain_image_count, sizeof(VkFence));

    // depth-only view for sampling
    VkImageViewCreateInfo depth_sampled_view_ci = {0};
    depth_sampled_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depth_sampled_view_ci.image = vulkan_state.depth_image;
    depth_sampled_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_sampled_view_ci.format = vulkan_state.depth_format;
    depth_sampled_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_sampled_view_ci.subresourceRange.baseMipLevel = 0;
    depth_sampled_view_ci.subresourceRange.levelCount = 1;
    depth_sampled_view_ci.subresourceRange.baseArrayLayer = 0;
    depth_sampled_view_ci.subresourceRange.layerCount = 1;

    vkCreateImageView(vulkan_state.logical_device_handle, &depth_sampled_view_ci, 0, &vulkan_state.depth_sampled_view);

    // UPDATE DESCRIPTOR SETS

    // update depth descriptor sets
    VkDescriptorImageInfo depth_desc_info = {0};
    depth_desc_info.sampler = vulkan_state.pixel_art_sampler;
    depth_desc_info.imageView = vulkan_state.depth_sampled_view;
    depth_desc_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet depth_desc_write = {0};
    depth_desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depth_desc_write.dstSet = vulkan_state.depth_descriptor_set;
    depth_desc_write.dstBinding = 0;
    depth_desc_write.descriptorCount = 1;
    depth_desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depth_desc_write.pImageInfo = &depth_desc_info;

    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &depth_desc_write, 0, 0);

    // update normal descriptor sets
    VkDescriptorImageInfo normal_desc_info = {0};
    normal_desc_info.sampler = vulkan_state.pixel_art_sampler;
    normal_desc_info.imageView = vulkan_state.normal_image_view;
    normal_desc_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet normal_desc_write = {0};
    normal_desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normal_desc_write.dstSet = vulkan_state.normal_descriptor_set;
    normal_desc_write.dstBinding = 0;
    normal_desc_write.descriptorCount = 1;
    normal_desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normal_desc_write.pImageInfo = &normal_desc_info;

    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &normal_desc_write, 0, 0);

    // update water depth descriptor set
    VkDescriptorImageInfo water_depth_desc_info = {0};
    water_depth_desc_info.sampler = vulkan_state.pixel_art_sampler;
    water_depth_desc_info.imageView = vulkan_state.water_depth_image_view;
    water_depth_desc_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet water_depth_desc_write = {0};
    water_depth_desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    water_depth_desc_write.dstSet = vulkan_state.water_depth_descriptor_set;
    water_depth_desc_write.dstBinding = 0;
    water_depth_desc_write.descriptorCount = 1;
    water_depth_desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    water_depth_desc_write.pImageInfo = &water_depth_desc_info;

    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &water_depth_desc_write, 0, 0);

    // update water composite descriptor set
    VkDescriptorImageInfo wc_desc_info = {0};
    wc_desc_info.sampler = vulkan_state.pixel_art_sampler;
    wc_desc_info.imageView = vulkan_state.scene_copy_image_view;
    wc_desc_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet wc_desc_write = {0};
    wc_desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wc_desc_write.dstSet = vulkan_state.scene_copy_descriptor_set;
    wc_desc_write.dstBinding = 0;
    wc_desc_write.descriptorCount = 1;
    wc_desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wc_desc_write.pImageInfo = &wc_desc_info;

    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &wc_desc_write, 0, 0); 

    // OIT head image
    {
        VkDescriptorImageInfo image_info = {0};
        image_info.imageView = vulkan_state.oit_head_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.oit_head_storage_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // OIT fragment pool
    {
        VkDescriptorBufferInfo buf_info = {0};
        buf_info.buffer = vulkan_state.oit_fragment_pool;
        buf_info.offset = 0;
        buf_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.oit_fragment_pool_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buf_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // OIT counter
    {
        VkDescriptorBufferInfo buf_info = {0};
        buf_info.buffer = vulkan_state.oit_counter_buffer;
        buf_info.offset = 0;
        buf_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.oit_counter_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buf_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // reflections
    VkDescriptorImageInfo reflection_desc_info = {0};
    reflection_desc_info.sampler = vulkan_state.linear_clamp_sampler;
    reflection_desc_info.imageView = vulkan_state.reflection_color_image_view;
    reflection_desc_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet reflection_desc_write = {0};
    reflection_desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    reflection_desc_write.dstSet = vulkan_state.reflection_descriptor_set;
    reflection_desc_write.dstBinding = 0;
    reflection_desc_write.descriptorCount = 1;
    reflection_desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    reflection_desc_write.pImageInfo = &reflection_desc_info;

    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &reflection_desc_write, 0, 0);

    // FRAMEBUFFERS

    // main scene framebuffers
    vulkan_state.swapchain_framebuffers = realloc(vulkan_state.swapchain_framebuffers, sizeof(VkFramebuffer) * vulkan_state.swapchain_image_count);

    for (uint32 framebuffer_index = 0; framebuffer_index < vulkan_state.swapchain_image_count; framebuffer_index++)
    {
        VkImageView framebuffer_attachments[3] = { vulkan_state.swapchain_image_views[framebuffer_index], vulkan_state.depth_image_view, vulkan_state.normal_image_view };

        VkFramebufferCreateInfo framebuffer_ci = {0};
        framebuffer_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_ci.renderPass = vulkan_state.render_pass_handle;
        framebuffer_ci.attachmentCount = 3;
        framebuffer_ci.pAttachments = framebuffer_attachments;
        framebuffer_ci.width = vulkan_state.swapchain_extent.width;
        framebuffer_ci.height = vulkan_state.swapchain_extent.height;
        framebuffer_ci.layers = 1;

        vkCreateFramebuffer(vulkan_state.logical_device_handle, &framebuffer_ci, 0, &vulkan_state.swapchain_framebuffers[framebuffer_index]);
    }

    // shadow framebuffer
    {
        VkImageView shadow_attachments[1] = { vulkan_state.shadow_map_image_view };

        VkFramebufferCreateInfo shadow_framebuffer_ci = {0};
        shadow_framebuffer_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        shadow_framebuffer_ci.renderPass = vulkan_state.shadow_render_pass;
        shadow_framebuffer_ci.attachmentCount = 1;
        shadow_framebuffer_ci.pAttachments = shadow_attachments;
        shadow_framebuffer_ci.width = SHADOW_MAP_RESOLUTION;
        shadow_framebuffer_ci.height = SHADOW_MAP_RESOLUTION;
        shadow_framebuffer_ci.layers = 1;

        vkCreateFramebuffer(vulkan_state.logical_device_handle, &shadow_framebuffer_ci, 0, &vulkan_state.shadow_framebuffer);
    }

    // post-process framebuffers
    vulkan_state.outline_post_framebuffers = realloc(vulkan_state.outline_post_framebuffers, sizeof(VkFramebuffer) * vulkan_state.swapchain_image_count);

    for (uint32 i = 0; i < vulkan_state.swapchain_image_count; i++)
    {
        VkImageView attachment = vulkan_state.swapchain_image_views[i];

        VkFramebufferCreateInfo fb_ci = {0};
        fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass = vulkan_state.outline_post_render_pass;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments = &attachment;
        fb_ci.width = vulkan_state.swapchain_extent.width;
        fb_ci.height = vulkan_state.swapchain_extent.height;
        fb_ci.layers = 1;

        vkCreateFramebuffer(vulkan_state.logical_device_handle, &fb_ci, 0, &vulkan_state.outline_post_framebuffers[i]);
    }

    // overlay framebuffers
    vulkan_state.overlay_framebuffers = realloc(vulkan_state.overlay_framebuffers, sizeof(VkFramebuffer) * vulkan_state.swapchain_image_count);

    for (uint32 i = 0; i < vulkan_state.swapchain_image_count; i++)
    {
        VkImageView overlay_fb_attachments[2] = { vulkan_state.swapchain_image_views[i], vulkan_state.depth_image_view };

        VkFramebufferCreateInfo overlay_fb_ci = {0};
        overlay_fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        overlay_fb_ci.renderPass = vulkan_state.overlay_render_pass;
        overlay_fb_ci.attachmentCount = 2;
        overlay_fb_ci.pAttachments = overlay_fb_attachments;
        overlay_fb_ci.width = vulkan_state.swapchain_extent.width;
        overlay_fb_ci.height = vulkan_state.swapchain_extent.height;
        overlay_fb_ci.layers = 1;

        vkCreateFramebuffer(vulkan_state.logical_device_handle, &overlay_fb_ci, 0, &vulkan_state.overlay_framebuffers[i]);
    }

    // water framebuffers
	vulkan_state.water_framebuffers = realloc(vulkan_state.water_framebuffers, sizeof(VkFramebuffer) * vulkan_state.swapchain_image_count);

    for (uint32 i = 0; i < vulkan_state.swapchain_image_count; i++)
    {
        VkImageView water_fb_attachments[2] = 
        { 
            vulkan_state.swapchain_image_views[i], 
            vulkan_state.water_depth_image_view
        };

        VkFramebufferCreateInfo water_fb_ci = {0};
        water_fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        water_fb_ci.renderPass = vulkan_state.water_render_pass;
        water_fb_ci.attachmentCount = 2;
        water_fb_ci.pAttachments = water_fb_attachments;
        water_fb_ci.width = vulkan_state.swapchain_extent.width;
        water_fb_ci.height = vulkan_state.swapchain_extent.height;
        water_fb_ci.layers = 1;

        vkCreateFramebuffer(vulkan_state.logical_device_handle, &water_fb_ci, 0, &vulkan_state.water_framebuffers[i]);
    }

    // waterline framebuffers
    vulkan_state.waterline_framebuffers = malloc(sizeof(VkFramebuffer) * vulkan_state.swapchain_image_count);
    for (uint32 i = 0; i < vulkan_state.swapchain_image_count; i++)
    {
        VkImageView attachments[1] = { vulkan_state.swapchain_image_views[i] };

        VkFramebufferCreateInfo fb_ci = {0};
        fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass = vulkan_state.waterline_render_pass;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments = attachments;
        fb_ci.width = vulkan_state.swapchain_extent.width;
        fb_ci.height = vulkan_state.swapchain_extent.height;
        fb_ci.layers = 1;

        vkCreateFramebuffer(vulkan_state.logical_device_handle, &fb_ci, 0, &vulkan_state.waterline_framebuffers[i]);
    }

    // reflection framebuffer
    {
        VkImageView reflection_attachments[3] =
        {
            vulkan_state.reflection_msaa_color_image_view,
            vulkan_state.reflection_msaa_depth_image_view,
            vulkan_state.reflection_color_image_view,
        };

        VkFramebufferCreateInfo fb_ci = {0};
        fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass = vulkan_state.reflection_render_pass;
        fb_ci.attachmentCount = 3;
        fb_ci.pAttachments = reflection_attachments;
        fb_ci.width = reflection_width;
        fb_ci.height = reflection_height;
        fb_ci.layers = 1;

        vkCreateFramebuffer(vulkan_state.logical_device_handle, &fb_ci, 0, &vulkan_state.reflection_framebuffer);
    }
}

void resetPipelineStates(VkPipelineColorBlendAttachmentState* blend, VkPipelineDepthStencilStateCreateInfo* depth_stencil, VkPipelineRasterizationStateCreateInfo* raster)
{
    // blend: opaque, no blending
    blend->blendEnable = VK_FALSE;
    blend->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend->dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend->colorBlendOp = VK_BLEND_OP_ADD;
    blend->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend->alphaBlendOp = VK_BLEND_OP_ADD;
    blend->colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // depth: test + write, less
    depth_stencil->depthTestEnable = VK_TRUE;
    depth_stencil->depthWriteEnable = VK_TRUE;
    depth_stencil->depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil->depthBoundsTestEnable = VK_FALSE;

    // stencil: off
    depth_stencil->stencilTestEnable = VK_FALSE;
    depth_stencil->front.failOp = VK_STENCIL_OP_KEEP;
    depth_stencil->front.passOp = VK_STENCIL_OP_KEEP;
    depth_stencil->front.depthFailOp = VK_STENCIL_OP_KEEP;
    depth_stencil->front.compareOp = VK_COMPARE_OP_ALWAYS;
    depth_stencil->front.compareMask = 0xFF;
    depth_stencil->front.writeMask = 0x00;
    depth_stencil->front.reference = 0;
    depth_stencil->back = depth_stencil->front;

    // rasterization: filled, no cull
    raster->polygonMode = VK_POLYGON_MODE_FILL;
    raster->cullMode = VK_CULL_MODE_NONE;
    raster->frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster->lineWidth = 1.0f;
    raster->depthBiasEnable = VK_FALSE;
}

void vulkanInitialize(RendererPlatformHandles platform_handles, DisplayInfo display)
{
    vulkan_display = display;

    vulkan_state.platform_handles = platform_handles;
    vulkan_state.vulkan_instance_handle = VK_NULL_HANDLE;
    vulkan_state.surface_handle = VK_NULL_HANDLE;
    vulkan_state.physical_device_handle = VK_NULL_HANDLE;

    char* instance_extensions[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };

    // only enable validation if available on device
    uint32 layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, 0);
    VkLayerProperties* available = malloc(sizeof(VkLayerProperties) * layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available);

    char* validation_layers[] = { "VK_LAYER_KHRONOS_validation" };
    bool has_validation = false;
    for (uint32 layer_index = 0; layer_index < layer_count; layer_index++)
    {
        if (strcmp(available[layer_index].layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            has_validation = true;
            break;
        }
    }
    free(available);

	VkApplicationInfo api_info = {0};
    api_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    api_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_ci = {0};
    instance_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_ci.pApplicationInfo = &api_info;
    instance_ci.enabledExtensionCount = 2;
    instance_ci.ppEnabledExtensionNames = instance_extensions;
    instance_ci.enabledLayerCount = has_validation ? 1 : 0;
    instance_ci.ppEnabledLayerNames = has_validation ? validation_layers : 0;

    vkCreateInstance(&instance_ci, 0, &vulkan_state.vulkan_instance_handle);

	VkWin32SurfaceCreateInfoKHR surface_creation_info = {0};
    surface_creation_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_creation_info.hinstance = vulkan_state.platform_handles.module_handle;
    surface_creation_info.hwnd = vulkan_state.platform_handles.window_handle;

	vkCreateWin32SurfaceKHR(vulkan_state.vulkan_instance_handle, &surface_creation_info, 0, &vulkan_state.surface_handle);

	uint32 device_count = 0;
    vkEnumeratePhysicalDevices(vulkan_state.vulkan_instance_handle, &device_count, 0);

	if (device_count == 0) return;

	VkPhysicalDevice* physical_devices = malloc(sizeof(*physical_devices) * device_count);
	vkEnumeratePhysicalDevices(vulkan_state.vulkan_instance_handle, &device_count, physical_devices);
	
	// loop over devices to pick one that 1. does graphics and 2. can present to win32 surface
    for (uint32 device_increment = 0; device_increment < device_count; device_increment++)
    {
        // hardware queue = the GPU's execution lane - FIFO where the driver submits recorded
        // command buffers to be executed.

        // a queue family is a group of hardware queues on GPU that support the same capabilities.

        // we need a family whose queueFlags include 'can do graphics' (VK_QUEUE_GRAPHICS_BIT) and
        // one that can present to Win32 surface, which is queried per family with a function.
        // sometimes one family satisfies both; sometimes we need two families.

		uint32 family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[device_increment], &family_count, 0);
        VkQueueFamilyProperties* families = malloc(sizeof(*families) * family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[device_increment], &family_count, families);

        int local_graphics_family_index = -1;
        int local_present_family_index = -1;

		for (uint32 family_increment = 0; family_increment < family_count; family_increment++)
        {
			if (local_graphics_family_index == -1 && (families[family_increment].queueFlags & VK_QUEUE_GRAPHICS_BIT)) local_graphics_family_index = (int)family_increment;

			VkBool32 can_present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[device_increment], family_increment, vulkan_state.surface_handle, &can_present);

            if (local_present_family_index == -1 && can_present) local_present_family_index = (int)family_increment;

            // NOTE: some inconsistencies with where properties are checked, but this is
            // 		 still safe, just a bit less readable. also avoids redoing some checks.
            if (local_graphics_family_index != -1 && local_present_family_index != -1)
            {
                uint32 device_extension_count = 0;
                vkEnumerateDeviceExtensionProperties(physical_devices[device_increment], 0, &device_extension_count, 0);
                VkExtensionProperties* extensions = malloc(sizeof(*extensions) * device_extension_count); // NOTE: may be malloc(0)
                vkEnumerateDeviceExtensionProperties(physical_devices[device_increment], 0, &device_extension_count, extensions);

                bool has_swapchain = false;

                for (uint32 extension_increment = 0; extension_increment < device_extension_count; extension_increment++)
                {
					if (strcmp(extensions[extension_increment].extensionName, "VK_KHR_swapchain") == 0)
                    {
                        has_swapchain = true;
                        break;
                    }
                }
                free(extensions);
                
                if (has_swapchain)
                {
                    vulkan_state.physical_device_handle = physical_devices[device_increment];
                    vulkan_state.graphics_family_index = (uint32)local_graphics_family_index;
                    vulkan_state.present_family_index = (uint32)local_present_family_index;
                }
                break;
           	}
        }
        free(families);
		
        if (vulkan_state.physical_device_handle != VK_NULL_HANDLE) break;
    }
    free(physical_devices);

    uint32 present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_state.physical_device_handle, vulkan_state.surface_handle, &present_mode_count, 0);
    
    if (present_mode_count == 0) return;

    VkPresentModeKHR* present_modes = malloc(sizeof(*present_modes) * present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_state.physical_device_handle, vulkan_state.surface_handle, &present_mode_count, present_modes);

    //VkPresentModeKHR chosen_present_mode = VK_PRESENT_MODE_FIFO_KHR; 
    VkPresentModeKHR chosen_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR; 

    if (vulkan_display.refresh_rate > 60)
    {
        // on high refresh rate monitors prefer mailbox
        for (uint32 present_mode_increment = 0; present_mode_increment < present_mode_count; present_mode_increment++)
        {
            if (present_modes[present_mode_increment] == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                chosen_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }
    }
    free(present_modes);

    VkSurfaceCapabilitiesKHR surface_capabilities = {0}; // constraints and options for this device/surface pair, reported by WSI.
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_state.physical_device_handle, vulkan_state.surface_handle, &surface_capabilities);

    uint32 surface_format_count = 0; // the allowed (pixel format, color space) pairs for images to present.
    vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_state.physical_device_handle, vulkan_state.surface_handle, &surface_format_count, 0);

	if (surface_format_count == 0) return;

    VkSurfaceFormatKHR* surface_formats = malloc(sizeof(*surface_formats) * surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_state.physical_device_handle, vulkan_state.surface_handle, &surface_format_count, surface_formats);

    VkSurfaceFormatKHR chosen_surface_format = surface_formats[0]; // some random guaranteed - will now overwrite if possible

	// TODO: handle edge case where only one surface format, equal to VK_FORMAT_UNDEFINED
    for (uint32 surface_format_increment = 0; surface_format_increment < surface_format_count; surface_format_increment++)
    {
		if (surface_formats[surface_format_increment].format == VK_FORMAT_B8G8R8A8_SRGB && surface_formats[surface_format_increment].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosen_surface_format = surface_formats[surface_format_increment];
            break;
        }
    }
    free(surface_formats);
	
    vulkan_state.swapchain_format = chosen_surface_format.format;

    // queue family = a group of queues with the same capabilities (graphics, compute, transfer...)

    // VkQueue = one submision lane from that family. submit command buffers to a queue; within a
    // queue, commands execute in submission order.

    // we allocate command buffers from a command pool tied to that family, and we may only submit
    // those buffers to queues of that same family

    // different queues (especially from different families/engines) can run overlapped, sometimes.

    // at device creation, for each family we request queues from, provide pQueuePriorities: one
    // float per queue, in [0,1]. this is set at creation for the entire runtime.

    // these are relative hints used when various queues in the same family are trying to use the 
    //
    // same engine. for now, i will only have one queue, so i'll put its priority at 1.0f
    //
	float queue_priorities[1] = { 1.0f };

    // struct to get x queues from family y, with these priorities.
    //
    // this struct is one per family i want queues from, so i will have one or two of these
    //
    VkDeviceQueueCreateInfo graphics_queue_info = {0};

    graphics_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphics_queue_info.queueFamilyIndex = vulkan_state.graphics_family_index;
    graphics_queue_info.queueCount = 1;
    graphics_queue_info.pQueuePriorities = queue_priorities;

    VkDeviceQueueCreateInfo queue_family_infos[2] = { graphics_queue_info };

    // check if we need a separate VkDeviceQueueCreateInfo for present capabilities;
    // i.e., if present family differs from graphics family, and so we have two queues
    bool graphics_present_families_same = (vulkan_state.present_family_index == vulkan_state.graphics_family_index);

    if (!graphics_present_families_same)
    {
        VkDeviceQueueCreateInfo present_queue_info = {0};
        present_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        present_queue_info.queueFamilyIndex = vulkan_state.present_family_index;
        present_queue_info.queueCount = 1;
        present_queue_info.pQueuePriorities = queue_priorities;

        queue_family_infos[1] = present_queue_info;
    }

    // we will need to pass the device extensions we want the logical device to use
    const char* device_extensions[] = { "VK_KHR_swapchain" };

    VkPhysicalDeviceFeatures device_features = {0};
    device_features.fillModeNonSolid = VK_TRUE;
    device_features.wideLines = VK_TRUE;
    device_features.fragmentStoresAndAtomics = VK_TRUE;
    device_features.independentBlend = VK_TRUE;
    device_features.textureCompressionBC = VK_TRUE;

    VkDeviceCreateInfo device_info = {0}; // struct that bundles everthing the driver needs to create the logical device
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = (graphics_present_families_same ? 1u : 2u);
    device_info.pQueueCreateInfos = queue_family_infos;
    device_info.enabledExtensionCount = 1;
	device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &device_features;

    vkCreateDevice(vulkan_state.physical_device_handle, &device_info, 0, &vulkan_state.logical_device_handle);

    // profiling
    VkPhysicalDeviceProperties device_properties = {0};
    vkGetPhysicalDeviceProperties(vulkan_state.physical_device_handle, &device_properties);
    vulkan_state.timestamp_period = device_properties.limits.timestampPeriod;

    vulkan_state.timestamp_query_count = 32;
    for (int frame_index = 0; frame_index < 3; frame_index++)
    {
        VkQueryPoolCreateInfo qp_ci = {0};
        qp_ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qp_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qp_ci.queryCount = vulkan_state.timestamp_query_count;
        vkCreateQueryPool(vulkan_state.logical_device_handle, &qp_ci, 0, &vulkan_state.timestamp_query_pools[frame_index]);
        vulkan_state.timestamp_pool_valid[frame_index] = false;
    }

    vulkan_state.depth_format = VK_FORMAT_D32_SFLOAT;

    // TODO: organise this better. the normal attachment is under 'first render pass' here.
    // scene render pass
    {
        VkAttachmentDescription color_attachment = {0};
        color_attachment.format = chosen_surface_format.format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT; // no multi-sampling anti-aliasing, so only one color sample
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // start each frame by clearing the swapchain image to a solid color
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // we want the image to be read by the present engine after the render pass
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // don't care about previous layout of swapchain image
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        
        VkAttachmentReference color_attachment_reference = {0}; // tells the subpass which attachment slot and in what layout during the subpass.
        color_attachment_reference.attachment = 0; // to be explicit about that we are getting the first attachment
        color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // write color layout in optimal layout for color output
        
        VkAttachmentDescription depth_attachment = {0};
        depth_attachment.format = vulkan_state.depth_format;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // store for second render pass
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; 
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_attachment_reference = {0};
        depth_attachment_reference.attachment = 1; // second attachment in array
        depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription color_output_subpass = {0}; // only one subpass per frame for our minimal setup
        color_output_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        color_output_subpass.colorAttachmentCount = 1;
        color_output_subpass.pColorAttachments = &color_attachment_reference;
        color_output_subpass.pDepthStencilAttachment = &depth_attachment_reference;
        
        VkSubpassDependency color_output_subpass_dependency = {0}; // encodes memory + exectution ordering between stages outside the render pass and stages inside the subpass
        color_output_subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // declares source as 'outside the render pass' - there's only one subpass so the source has to be external here
        color_output_subpass_dependency.dstSubpass = 0; // set first (and only) subpass as destination of the dependency
        color_output_subpass_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // some ordering shenanigans
        color_output_subpass_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        color_output_subpass_dependency.srcAccessMask = 0; // we don't rely on any prior contents
        color_output_subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // we want to protect the attachment at the destination

        VkAttachmentDescription normal_attachment = {0};
        normal_attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        normal_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        normal_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        normal_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        normal_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        normal_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        normal_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        normal_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference normal_attachment_reference = {0};
        normal_attachment_reference.attachment = 2;
        normal_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_attachment_references[2] = { color_attachment_reference, normal_attachment_reference };

        color_output_subpass.colorAttachmentCount = 2;
        color_output_subpass.pColorAttachments = color_attachment_references;

        VkAttachmentDescription attachments[3] = { color_attachment, depth_attachment, normal_attachment };

        VkRenderPassCreateInfo render_pass_creation_info = {0}; // container that ties attachment(s), subpass(es), and dependency(ies) into a single render pass object.
        render_pass_creation_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_creation_info.attachmentCount = 3;
        render_pass_creation_info.pAttachments = attachments;
        render_pass_creation_info.subpassCount = 1;
        render_pass_creation_info.pSubpasses = &color_output_subpass;
        render_pass_creation_info.dependencyCount = 1;
        render_pass_creation_info.pDependencies = &color_output_subpass_dependency; // same story here - just a pointer to our one dependency, rather than an array.

        vkCreateRenderPass(vulkan_state.logical_device_handle, &render_pass_creation_info, 0, &vulkan_state.render_pass_handle);
    }

    // second render pass for outlines
    {
        VkAttachmentDescription post_color_attachment = {0};
        post_color_attachment.format = vulkan_state.swapchain_format;
        post_color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        post_color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // keep scene contents
        post_color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        post_color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        post_color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        post_color_attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        post_color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference post_color_reference = {0};
        post_color_reference.attachment = 0;
        post_color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription post_subpass = {0};
        post_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        post_subpass.colorAttachmentCount = 1;
        post_subpass.pColorAttachments = &post_color_reference;

        VkSubpassDependency post_dependency = {0};
        post_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        post_dependency.dstSubpass = 0;
        post_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        post_dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        post_dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        post_dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo post_render_pass_ci = {0};
        post_render_pass_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        post_render_pass_ci.attachmentCount = 1;
        post_render_pass_ci.pAttachments = &post_color_attachment;
        post_render_pass_ci.subpassCount = 1;
        post_render_pass_ci.pSubpasses = &post_subpass;
        post_render_pass_ci.dependencyCount = 1;
        post_render_pass_ci.pDependencies = &post_dependency;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &post_render_pass_ci, 0, &vulkan_state.outline_post_render_pass);
    }

    // overlay render pass for lasers and sprites
    {
        VkAttachmentDescription overlay_attachments[2] = {0};

        overlay_attachments[0].format = vulkan_state.swapchain_format;
        overlay_attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        overlay_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        overlay_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        overlay_attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        overlay_attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        overlay_attachments[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        overlay_attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        overlay_attachments[1].format = vulkan_state.depth_format;
        overlay_attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        overlay_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        overlay_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        overlay_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        overlay_attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        overlay_attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        overlay_attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference overlay_color_reference = {0};
        overlay_color_reference.attachment = 0;
        overlay_color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference overlay_depth_reference = {0};
        overlay_depth_reference.attachment = 1;
        overlay_depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkSubpassDescription overlay_subpass = {0};
        overlay_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        overlay_subpass.colorAttachmentCount = 1;
        overlay_subpass.pColorAttachments = &overlay_color_reference;
        overlay_subpass.pDepthStencilAttachment = &overlay_depth_reference;

        VkSubpassDependency overlay_dependencies[2] = {0};

        overlay_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        overlay_dependencies[0].dstSubpass = 0;
        overlay_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        overlay_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        overlay_dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        overlay_dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        overlay_dependencies[1].srcSubpass = 0;
        overlay_dependencies[1].dstSubpass = 0;
        overlay_dependencies[1].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        overlay_dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        overlay_dependencies[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        overlay_dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        overlay_dependencies[1].dependencyFlags = 0;

        VkRenderPassCreateInfo overlay_rp_ci = {0};
        overlay_rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        overlay_rp_ci.attachmentCount = 2;
        overlay_rp_ci.pAttachments = overlay_attachments;
        overlay_rp_ci.subpassCount = 1;
        overlay_rp_ci.pSubpasses = &overlay_subpass;
        overlay_rp_ci.dependencyCount = 2;
        overlay_rp_ci.pDependencies = overlay_dependencies;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &overlay_rp_ci, 0, &vulkan_state.overlay_render_pass);
    }

    // water render pass
    {
        VkAttachmentDescription water_attachments[2] = {0};

        water_attachments[0].format = vulkan_state.swapchain_format;
        water_attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        water_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        water_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        water_attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        water_attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        water_attachments[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        water_attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        water_attachments[1].format = VK_FORMAT_R32_SFLOAT;
        water_attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        water_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        water_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        water_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        water_attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        water_attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        water_attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference water_color_refs[2] = {0};
        water_color_refs[0].attachment = 0;
        water_color_refs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        water_color_refs[1].attachment = 1;
        water_color_refs[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription water_subpass = {0};
        water_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        water_subpass.colorAttachmentCount = 2;
        water_subpass.pColorAttachments = water_color_refs;
        water_subpass.pDepthStencilAttachment = 0;

        VkSubpassDependency water_dependencies[2] = {0};

        water_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        water_dependencies[0].dstSubpass = 0;
        water_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        water_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        water_dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        water_dependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        water_dependencies[1].srcSubpass = 0;
        water_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        water_dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        water_dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        water_dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        water_dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo water_rp_ci = {0};
        water_rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        water_rp_ci.attachmentCount = 2;
        water_rp_ci.pAttachments = water_attachments;
        water_rp_ci.subpassCount = 1;
        water_rp_ci.pSubpasses = &water_subpass;
        water_rp_ci.dependencyCount = 1;
        water_rp_ci.pDependencies = water_dependencies;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &water_rp_ci, 0, &vulkan_state.water_render_pass);
    }

    // waterline render pass
    {
        VkAttachmentDescription color_attachment = {0};
        color_attachment.format = vulkan_state.swapchain_format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_ref = {0};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {0};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;

        VkSubpassDependency dep = {0};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp_ci = {0};
        rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_ci.attachmentCount = 1;
        rp_ci.pAttachments = &color_attachment;
        rp_ci.subpassCount = 1;
        rp_ci.pSubpasses = &subpass;
        rp_ci.dependencyCount = 1;
        rp_ci.pDependencies = &dep;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &rp_ci, 0, &vulkan_state.waterline_render_pass);
    }

    // reflected scene render pass
    {
        VkAttachmentDescription reflection_attachments[3] = {0};

        // MSAA color
        reflection_attachments[0].format = vulkan_state.swapchain_format;
        reflection_attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
        reflection_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        reflection_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        reflection_attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        reflection_attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        reflection_attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        reflection_attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // MSAA depth
        reflection_attachments[1].format = vulkan_state.depth_format;
        reflection_attachments[1].samples = VK_SAMPLE_COUNT_4_BIT;
        reflection_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        reflection_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        reflection_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        reflection_attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        reflection_attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        reflection_attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // resolve target
        reflection_attachments[2].format = vulkan_state.swapchain_format;
        reflection_attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        reflection_attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        reflection_attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        reflection_attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        reflection_attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        reflection_attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        reflection_attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference reflection_color_ref = {0};
        reflection_color_ref.attachment = 0;
        reflection_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference reflection_depth_ref = {0};
        reflection_depth_ref.attachment = 1;
        reflection_depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference reflection_resolve_ref = {0};
        reflection_resolve_ref.attachment = 2;
        reflection_resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription reflection_subpass = {0};
        reflection_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        reflection_subpass.colorAttachmentCount = 1;
        reflection_subpass.pColorAttachments = &reflection_color_ref;
        reflection_subpass.pResolveAttachments = &reflection_resolve_ref;
        reflection_subpass.pDepthStencilAttachment = &reflection_depth_ref;

        VkSubpassDependency reflection_dependencies[2] = {0};

        reflection_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        reflection_dependencies[0].dstSubpass = 0;
        reflection_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        reflection_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        reflection_dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        reflection_dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        reflection_dependencies[1].srcSubpass = 0;
        reflection_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        reflection_dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        reflection_dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        reflection_dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        reflection_dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rp_ci = {0};
        rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_ci.attachmentCount = 3;
        rp_ci.pAttachments = reflection_attachments;
        rp_ci.subpassCount = 1;
        rp_ci.pSubpasses = &reflection_subpass;
        rp_ci.dependencyCount = 2;
        rp_ci.pDependencies = reflection_dependencies;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &rp_ci, 0, &vulkan_state.reflection_render_pass);
    }

    // SHADOW RENDER PASS
    {
        VkAttachmentDescription shadow_depth_attachment = {0};
        shadow_depth_attachment.format = vulkan_state.depth_format;
        shadow_depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        shadow_depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        shadow_depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        shadow_depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        shadow_depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        shadow_depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        shadow_depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference shadow_depth_reference = {0};
        shadow_depth_reference.attachment = 0;
        shadow_depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription shadow_subpass = {0};
        shadow_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        shadow_subpass.colorAttachmentCount = 0;
        shadow_subpass.pColorAttachments = 0;
        shadow_subpass.pDepthStencilAttachment = &shadow_depth_reference;

        VkSubpassDependency shadow_dependencies[2] = {0};

        shadow_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        shadow_dependencies[0].dstSubpass = 0;
        shadow_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        shadow_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        shadow_dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        shadow_dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // depth writes must be visible
        shadow_dependencies[1].srcSubpass = 0;
        shadow_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        shadow_dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        shadow_dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        shadow_dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        shadow_dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo shadow_render_pass_ci = {0};
        shadow_render_pass_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        shadow_render_pass_ci.attachmentCount = 1;
        shadow_render_pass_ci.pAttachments = &shadow_depth_attachment;
        shadow_render_pass_ci.subpassCount = 1;
        shadow_render_pass_ci.pSubpasses = &shadow_subpass;
        shadow_render_pass_ci.dependencyCount = 2;
        shadow_render_pass_ci.pDependencies = shadow_dependencies;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &shadow_render_pass_ci, 0, &vulkan_state.shadow_render_pass);
    }

	VkCommandPoolCreateInfo command_pool_creation_info = {0}; // describes the command pool tied to graphics queue family
	command_pool_creation_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_creation_info.queueFamilyIndex = vulkan_state.graphics_family_index;
    command_pool_creation_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // lets us reset / rerecord command buffers

	vkCreateCommandPool(vulkan_state.logical_device_handle, &command_pool_creation_info, 0, &vulkan_state.graphics_command_pool_handle);

    vulkan_state.frames_in_flight = 2;
	vulkan_state.current_frame = 0;
	vulkan_state.image_available_semaphores = malloc(sizeof(VkSemaphore) * vulkan_state.frames_in_flight);
    vulkan_state.render_finished_semaphores = malloc(sizeof(VkSemaphore) * vulkan_state.frames_in_flight);
    vulkan_state.in_flight_fences = malloc(sizeof(VkFence) * vulkan_state.frames_in_flight);
											
    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32 frames_in_flight_increment = 0; frames_in_flight_increment < vulkan_state.frames_in_flight; frames_in_flight_increment++)
    {
        vkCreateSemaphore(vulkan_state.logical_device_handle, &semaphore_info, 0, &vulkan_state.image_available_semaphores[frames_in_flight_increment]);
        vkCreateSemaphore(vulkan_state.logical_device_handle, &semaphore_info, 0, &vulkan_state.render_finished_semaphores[frames_in_flight_increment]);
        vkCreateFence(vulkan_state.logical_device_handle, &fence_info, 0, &vulkan_state.in_flight_fences[frames_in_flight_increment]);
	}

    // used in the draw loop:

    // these might return the same handle, but that's fine
    vkGetDeviceQueue(vulkan_state.logical_device_handle, vulkan_state.graphics_family_index, 0, &vulkan_state.graphics_queue_handle);
    vkGetDeviceQueue(vulkan_state.logical_device_handle, vulkan_state.present_family_index, 0, &vulkan_state.present_queue_handle);

	// STAGE AND UPLOAD VERTEX / INDEX BUFFER FOR SPRITES AND CUBES
    vulkan_state.sprite_index_count = 6;
	uploadBufferToLocalDevice(SPRITE_VERTICES, sizeof(SPRITE_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vulkan_state.sprite_vertex_buffer, &vulkan_state.sprite_vertex_memory);
	uploadBufferToLocalDevice(SPRITE_INDICES,  sizeof(SPRITE_INDICES),  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  &vulkan_state.sprite_index_buffer,  &vulkan_state.sprite_index_memory);

	vulkan_state.cube_index_count = 36;
	uploadBufferToLocalDevice(CUBE_VERTICES, sizeof(CUBE_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vulkan_state.cube_vertex_buffer, &vulkan_state.cube_vertex_memory);
	uploadBufferToLocalDevice(CUBE_INDICES,  sizeof(CUBE_INDICES),  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  &vulkan_state.cube_index_buffer,  &vulkan_state.cube_index_memory);

    //vulkan_state.images_in_flight = calloc(vulkan_state.swapchain_image_count, sizeof(VkFence)); // calloc because we want these to start at VK_NULL_HANDLE, i.e. 0.

    // TODO: this isnt in createSwapchainResources because it needs to exist before then... is this the best place for it, though?
    // shadow map image
    {
        VkImageCreateInfo shadow_map_image_ci = {0};
        shadow_map_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        shadow_map_image_ci.imageType = VK_IMAGE_TYPE_2D;
        shadow_map_image_ci.extent.width  = SHADOW_MAP_RESOLUTION;
        shadow_map_image_ci.extent.height = SHADOW_MAP_RESOLUTION;
        shadow_map_image_ci.extent.depth  = 1;
        shadow_map_image_ci.mipLevels = 1;
        shadow_map_image_ci.arrayLayers = 1;
        shadow_map_image_ci.format = vulkan_state.depth_format;
        shadow_map_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        shadow_map_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        shadow_map_image_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        shadow_map_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        shadow_map_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &shadow_map_image_ci, 0, &vulkan_state.shadow_map_image);

        VkMemoryRequirements shadow_map_memory_requirements = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.shadow_map_image, &shadow_map_memory_requirements);

        VkMemoryAllocateInfo shadow_map_alloc = {0};
        shadow_map_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        shadow_map_alloc.allocationSize = shadow_map_memory_requirements.size;
        shadow_map_alloc.memoryTypeIndex = findMemoryType(shadow_map_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &shadow_map_alloc, 0, &vulkan_state.shadow_map_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.shadow_map_image, vulkan_state.shadow_map_image_memory, 0);

        VkImageViewCreateInfo shadow_map_view_ci = {0};
        shadow_map_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        shadow_map_view_ci.image = vulkan_state.shadow_map_image;
        shadow_map_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        shadow_map_view_ci.format = vulkan_state.depth_format;
        shadow_map_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        shadow_map_view_ci.subresourceRange.baseMipLevel = 0;
        shadow_map_view_ci.subresourceRange.levelCount = 1;
        shadow_map_view_ci.subresourceRange.baseArrayLayer = 0;
        shadow_map_view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &shadow_map_view_ci, 0, &vulkan_state.shadow_map_image_view);
    }

    // h0 spectrum image
    {
        VkImageCreateInfo h0_image_ci = {0};
        h0_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        h0_image_ci.imageType = VK_IMAGE_TYPE_2D;
        h0_image_ci.extent.width = FFT_SIZE;
        h0_image_ci.extent.height = FFT_SIZE;
        h0_image_ci.extent.depth = 1;
        h0_image_ci.mipLevels = 1;
        h0_image_ci.arrayLayers = 1;
        h0_image_ci.format = VK_FORMAT_R32G32_SFLOAT;
        h0_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        h0_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        h0_image_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        h0_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        h0_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &h0_image_ci, 0, &vulkan_state.h0_image);

        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.h0_image, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.h0_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.h0_image, vulkan_state.h0_image_memory, 0);

        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.h0_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_R32G32_SFLOAT; // complex number
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.h0_image_view);
    }

    // fft buffers spectrum image
    {
        VkImageCreateInfo image_ci = {0};
        image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_ci.imageType = VK_IMAGE_TYPE_2D;
        image_ci.extent.width = FFT_SIZE;
        image_ci.extent.height = FFT_SIZE;
        image_ci.extent.depth = 1;
        image_ci.mipLevels = 1;
        image_ci.arrayLayers = 1;
        image_ci.format = VK_FORMAT_R32G32_SFLOAT;
        image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &image_ci, 0, &vulkan_state.fft_buffer_a_image);
        vkCreateImage(vulkan_state.logical_device_handle, &image_ci, 0, &vulkan_state.fft_buffer_b_image);

        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.fft_buffer_a_image, &mem_req);

        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.fft_buffer_a_image_memory);
        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.fft_buffer_b_image_memory); // NOTE: using same alloc, almost certainly fine
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.fft_buffer_a_image, vulkan_state.fft_buffer_a_image_memory, 0);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.fft_buffer_b_image, vulkan_state.fft_buffer_b_image_memory, 0);

        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.fft_buffer_a_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_R32G32_SFLOAT;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.fft_buffer_a_image_view);

        view_ci.image = vulkan_state.fft_buffer_b_image;

        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.fft_buffer_b_image_view);
    }

    // water displacement image
    {
        VkImageCreateInfo image_ci = {0};
        image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_ci.imageType = VK_IMAGE_TYPE_2D;
        image_ci.extent.width = FFT_SIZE;
        image_ci.extent.height = FFT_SIZE;
        image_ci.extent.depth = 1;
        image_ci.mipLevels = 1;
        image_ci.arrayLayers = 1;
        image_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        vkCreateImage(vulkan_state.logical_device_handle, &image_ci, 0, &vulkan_state.displacement_image);
        
        VkMemoryRequirements mem_req = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.displacement_image, &mem_req);
        
        VkMemoryAllocateInfo alloc = {0};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.displacement_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.displacement_image, vulkan_state.displacement_image_memory, 0);
        
        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = vulkan_state.displacement_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;
        
        vkCreateImageView(vulkan_state.logical_device_handle, &view_ci, 0, &vulkan_state.displacement_image_view);
    }

    // LOADING SHADER MODULES

    VkShaderModule fft_spectrum_smh = {0};
    VkShaderModule fft_evolved_smh = {0};
    VkShaderModule fft_pass_smh = {0};
    VkShaderModule fft_finalize_smh = {0};
    VkShaderModule cube_vert_smh = {0};
    VkShaderModule cube_frag_smh = {0};
    VkShaderModule model_vert_smh = {0};
    VkShaderModule model_frag_smh = {0};
    VkShaderModule outline_post_vert_smh = {0};
    VkShaderModule outline_post_frag_smh = {0};
    VkShaderModule water_vert_smh = {0};
    VkShaderModule water_frag_smh = {0};
    VkShaderModule waterline_frag_smh = {0};
    VkShaderModule shadow_cube_vert_smh = {0};
    VkShaderModule shadow_model_vert_smh = {0};
    VkShaderModule laser_vert_smh = {0};
    VkShaderModule laser_frag_smh = {0};
    VkShaderModule oit_resolve_vert_smh = {0};
    VkShaderModule oit_resolve_frag_smh = {0};
    VkShaderModule outline_select_vert_smh = {0};
    VkShaderModule outline_select_frag_smh = {0};
    VkShaderModule sprite_vert_smh = {0};
    VkShaderModule sprite_frag_smh = {0};

    VkPipelineShaderStageCreateInfo fft_spectrum_stage_ci           = loadShaderStage("data/shaders/spirv/fft-spectrum.comp.spv",   &fft_spectrum_smh,          VK_SHADER_STAGE_COMPUTE_BIT);
    VkPipelineShaderStageCreateInfo fft_evolved_stage_ci            = loadShaderStage("data/shaders/spirv/fft-evolved.comp.spv",    &fft_evolved_smh,           VK_SHADER_STAGE_COMPUTE_BIT);
    VkPipelineShaderStageCreateInfo fft_pass_stage_ci               = loadShaderStage("data/shaders/spirv/fft-pass.comp.spv",       &fft_pass_smh,              VK_SHADER_STAGE_COMPUTE_BIT);
    VkPipelineShaderStageCreateInfo fft_finalize_stage_ci           = loadShaderStage("data/shaders/spirv/fft-finalize.comp.spv",   &fft_finalize_smh,          VK_SHADER_STAGE_COMPUTE_BIT);
    VkPipelineShaderStageCreateInfo cube_vert_stage_ci 	 	        = loadShaderStage("data/shaders/spirv/cube.vert.spv", 	  	  	&cube_vert_smh, 	  	    VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo cube_frag_stage_ci 	 	        = loadShaderStage("data/shaders/spirv/cube.frag.spv", 	  	  	&cube_frag_smh, 	  		VK_SHADER_STAGE_FRAGMENT_BIT);
	VkPipelineShaderStageCreateInfo model_vert_stage_ci 	 	    = loadShaderStage("data/shaders/spirv/model.vert.spv",   	  	&model_vert_smh,   		    VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo model_frag_stage_ci 	 	    = loadShaderStage("data/shaders/spirv/model.frag.spv",   	  	&model_frag_smh,   		    VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo outline_post_vert_stage_ci      = loadShaderStage("data/shaders/spirv/outline-post.vert.spv",   &outline_post_vert_smh,     VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo outline_post_frag_stage_ci      = loadShaderStage("data/shaders/spirv/outline-post.frag.spv",   &outline_post_frag_smh,     VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo water_vert_stage_ci             = loadShaderStage("data/shaders/spirv/water.vert.spv",          &water_vert_smh,            VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo water_frag_stage_ci             = loadShaderStage("data/shaders/spirv/water.frag.spv",          &water_frag_smh,            VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo waterline_frag_stage_ci         = loadShaderStage("data/shaders/spirv/waterline.frag.spv",      &waterline_frag_smh,        VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo shadow_cube_vert_stage_ci       = loadShaderStage("data/shaders/spirv/shadow-cube.vert.spv",    &shadow_cube_vert_smh,      VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo shadow_model_vert_stage_ci      = loadShaderStage("data/shaders/spirv/shadow-model.vert.spv",   &shadow_model_vert_smh,     VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo laser_vert_stage_ci             = loadShaderStage("data/shaders/spirv/laser.vert.spv",          &laser_vert_smh,            VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo laser_frag_stage_ci             = loadShaderStage("data/shaders/spirv/laser.frag.spv",          &laser_frag_smh,            VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo oit_resolve_vert_stage_ci       = loadShaderStage("data/shaders/spirv/oit-resolve.vert.spv",    &oit_resolve_vert_smh,      VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo oit_resolve_frag_stage_ci       = loadShaderStage("data/shaders/spirv/oit-resolve.frag.spv",    &oit_resolve_frag_smh,      VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo outline_select_vert_stage_ci    = loadShaderStage("data/shaders/spirv/outline-select.vert.spv", &outline_select_vert_smh, 	VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo outline_select_frag_stage_ci    = loadShaderStage("data/shaders/spirv/outline-select.frag.spv", &outline_select_frag_smh, 	VK_SHADER_STAGE_FRAGMENT_BIT);
	VkPipelineShaderStageCreateInfo sprite_vert_stage_ci  	        = loadShaderStage("data/shaders/spirv/sprite.vert.spv",  	  	&sprite_vert_smh,  		    VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo sprite_frag_stage_ci  	        = loadShaderStage("data/shaders/spirv/sprite.frag.spv",  	  	&sprite_frag_smh,  		    VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineShaderStageCreateInfo cube_shader_stages[2]  	 	    = { cube_vert_stage_ci,    	        cube_frag_stage_ci }; 
   	VkPipelineShaderStageCreateInfo model_shader_stages[2]   	    = { model_vert_stage_ci,   	        model_frag_stage_ci };
    VkPipelineShaderStageCreateInfo outline_post_shader_stages[2]   = { outline_post_vert_stage_ci,     outline_post_frag_stage_ci };
    VkPipelineShaderStageCreateInfo water_shader_stages[2]          = { water_vert_stage_ci,            water_frag_stage_ci };
    VkPipelineShaderStageCreateInfo waterline_shader_stages[2]      = { outline_post_vert_stage_ci,     waterline_frag_stage_ci };
    VkPipelineShaderStageCreateInfo laser_shader_stages[2]          = { laser_vert_stage_ci,            laser_frag_stage_ci };
    VkPipelineShaderStageCreateInfo oit_resolve_shader_stages[2]    = { oit_resolve_vert_stage_ci,      oit_resolve_frag_stage_ci };
    VkPipelineShaderStageCreateInfo outline_select_shader_stages[2] = { outline_select_vert_stage_ci,   outline_select_frag_stage_ci }; 
    VkPipelineShaderStageCreateInfo sprite_shader_stages[2]  	    = { sprite_vert_stage_ci,  	        sprite_frag_stage_ci };
    VkPipelineShaderStageCreateInfo shadow_cube_stages[1]           = { shadow_cube_vert_stage_ci };
    VkPipelineShaderStageCreateInfo shadow_model_stages[1]          = { shadow_model_vert_stage_ci };

    // vertex input
    // per-vertex data: used for individually drawn meshes (sprites, models (currently), lasers, etc.)
    // transform is set via push constant per draw call
    VkVertexInputBindingDescription vertex_binding_simple = {0};
    vertex_binding_simple.binding = 0;
    vertex_binding_simple.stride = sizeof(Vertex);
    vertex_binding_simple.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attributes_simple[4] = {0};

    vertex_attributes_simple[0].binding = 0;
    vertex_attributes_simple[0].location = 0;
    vertex_attributes_simple[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes_simple[0].offset = offsetof(Vertex, x);

    vertex_attributes_simple[1].binding = 0;
    vertex_attributes_simple[1].location = 1;
    vertex_attributes_simple[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertex_attributes_simple[1].offset = offsetof(Vertex, u);

    vertex_attributes_simple[2].binding = 0;
    vertex_attributes_simple[2].location = 2;
    vertex_attributes_simple[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes_simple[2].offset = offsetof(Vertex, nx);

    vertex_attributes_simple[3].binding = 0;
    vertex_attributes_simple[3].location = 3;
    vertex_attributes_simple[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes_simple[3].offset = offsetof(Vertex, r);

    VkPipelineVertexInputStateCreateInfo vertex_input_simple = {0};
    vertex_input_simple.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_simple.vertexBindingDescriptionCount = 1;
    vertex_input_simple.pVertexBindingDescriptions = &vertex_binding_simple;
    vertex_input_simple.vertexAttributeDescriptionCount = 4;
    vertex_input_simple.pVertexAttributeDescriptions = vertex_attributes_simple      ;

    // cube vertex input (instanced)
    VkVertexInputBindingDescription vertex_bindings_instanced[2] = {0};
    vertex_bindings_instanced[0].binding = 0;
    vertex_bindings_instanced[0].stride = sizeof(Vertex);
    vertex_bindings_instanced[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    vertex_bindings_instanced[1].binding = 1;
    vertex_bindings_instanced[1].stride = sizeof(CubeInstanceData);
    vertex_bindings_instanced[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription vertex_attributes_instanced[9] = {0};

    vertex_attributes_instanced[0].binding = 0;
    vertex_attributes_instanced[0].location = 0;
    vertex_attributes_instanced[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes_instanced[0].offset = offsetof(Vertex, x);

    vertex_attributes_instanced[1].binding = 0;
    vertex_attributes_instanced[1].location = 1;
    vertex_attributes_instanced[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertex_attributes_instanced[1].offset = offsetof(Vertex, u);

    vertex_attributes_instanced[2].binding = 0;
    vertex_attributes_instanced[2].location = 2;
    vertex_attributes_instanced[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes_instanced[2].offset = offsetof(Vertex, nx);

    vertex_attributes_instanced[3].binding = 0;
    vertex_attributes_instanced[3].location = 3;
    vertex_attributes_instanced[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes_instanced[3].offset = offsetof(Vertex, r);

    vertex_attributes_instanced[4].binding = 1;
    vertex_attributes_instanced[4].location = 4;
    vertex_attributes_instanced[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attributes_instanced[4].offset = offsetof(CubeInstanceData, model) + 0;

    vertex_attributes_instanced[5].binding = 1;
    vertex_attributes_instanced[5].location = 5;
    vertex_attributes_instanced[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attributes_instanced[5].offset = offsetof(CubeInstanceData, model) + 16;

    vertex_attributes_instanced[6].binding = 1;
    vertex_attributes_instanced[6].location = 6;
    vertex_attributes_instanced[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attributes_instanced[6].offset = offsetof(CubeInstanceData, model) + 32;

    vertex_attributes_instanced[7].binding = 1;
    vertex_attributes_instanced[7].location = 7;
    vertex_attributes_instanced[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attributes_instanced[7].offset = offsetof(CubeInstanceData, model) + 48;

    vertex_attributes_instanced[8].binding = 1;
    vertex_attributes_instanced[8].location = 8;
    vertex_attributes_instanced[8].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attributes_instanced[8].offset = offsetof(CubeInstanceData, uv_rect);

    VkPipelineVertexInputStateCreateInfo vertex_input_instanced = {0};
    vertex_input_instanced.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_instanced.vertexBindingDescriptionCount = 2;
    vertex_input_instanced.pVertexBindingDescriptions = vertex_bindings_instanced;
    vertex_input_instanced.vertexAttributeDescriptionCount = 9;
    vertex_input_instanced.pVertexAttributeDescriptions = vertex_attributes_instanced;

    // water vertex input (instanced)
    VkVertexInputBindingDescription water_bindings[2] = {0};
    water_bindings[0].binding = 0;
    water_bindings[0].stride = sizeof(Vertex);
    water_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    water_bindings[1].binding = 1;
    water_bindings[1].stride = sizeof(WaterInstanceData);
    water_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription water_attrs[8] = {0};

    water_attrs[0].binding = 0; water_attrs[0].location = 0;
    water_attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; water_attrs[0].offset = offsetof(Vertex, x);
    water_attrs[1].binding = 0; water_attrs[1].location = 1;
    water_attrs[1].format = VK_FORMAT_R32G32_SFLOAT; water_attrs[1].offset = offsetof(Vertex, u);
    water_attrs[2].binding = 0; water_attrs[2].location = 2;
    water_attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; water_attrs[2].offset = offsetof(Vertex, nx);
    water_attrs[3].binding = 0; water_attrs[3].location = 3;
    water_attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT; water_attrs[3].offset = offsetof(Vertex, r);

    water_attrs[4].binding = 1; water_attrs[4].location = 4;
    water_attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; water_attrs[4].offset = 0;
    water_attrs[5].binding = 1; water_attrs[5].location = 5;
    water_attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT; water_attrs[5].offset = 16;
    water_attrs[6].binding = 1; water_attrs[6].location = 6;
    water_attrs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT; water_attrs[6].offset = 32;
    water_attrs[7].binding = 1; water_attrs[7].location = 7;
    water_attrs[7].format = VK_FORMAT_R32G32B32A32_SFLOAT; water_attrs[7].offset = 48;

    VkPipelineVertexInputStateCreateInfo water_vertex_input = {0};
    water_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    water_vertex_input.vertexBindingDescriptionCount = 2;
    water_vertex_input.pVertexBindingDescriptions = water_bindings;
    water_vertex_input.vertexAttributeDescriptionCount = 8;
    water_vertex_input.pVertexAttributeDescriptions = water_attrs;

    // laser instancing
    VkVertexInputBindingDescription laser_bindings[2] = {0};
    laser_bindings[0].binding = 0;
    laser_bindings[0].stride = sizeof(Vertex);
    laser_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    laser_bindings[1].binding = 1;
    laser_bindings[1].stride = sizeof(LaserInstanceData);
    laser_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription laser_attributes[6] = {0};

    laser_attributes[0].binding = 0;
    laser_attributes[0].location = 0;
    laser_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    laser_attributes[0].offset = offsetof(Vertex, x);

    laser_attributes[1].binding = 1; // center.xyz + length.w
    laser_attributes[1].location = 4;
    laser_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    laser_attributes[1].offset = 0;

    laser_attributes[2].binding = 1; // rotation quaternion
    laser_attributes[2].location = 5;
    laser_attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    laser_attributes[2].offset = 16;

    laser_attributes[3].binding = 1; // color
    laser_attributes[3].location = 6;
    laser_attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    laser_attributes[3].offset = 32;

    laser_attributes[4].binding = 1; // start clip plane
    laser_attributes[4].location = 7;
    laser_attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    laser_attributes[4].offset = 48;

    laser_attributes[5].binding = 1; // end clip plane
    laser_attributes[5].location = 8;
    laser_attributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    laser_attributes[5].offset = 64;

    VkPipelineVertexInputStateCreateInfo laser_vertex_input = {0};
    laser_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    laser_vertex_input.vertexBindingDescriptionCount = 2;
    laser_vertex_input.pVertexBindingDescriptions = laser_bindings;
    laser_vertex_input.vertexAttributeDescriptionCount = 6;
    laser_vertex_input.pVertexAttributeDescriptions = laser_attributes;

    VkPipelineVertexInputStateCreateInfo empty_vertex_input = {0};
    empty_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_creation_info = {0}; 
    input_assembly_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state_creation_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state_creation_info.primitiveRestartEnable = VK_FALSE;

    VkViewport dummy_viewport = {0};
    VkRect2D dummy_scissor = {0};

    VkPipelineViewportStateCreateInfo viewport_state_creation_info = {0}; // describes viewport for the pipeline. set at drawtime.
    viewport_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_creation_info.viewportCount = 1;
    viewport_state_creation_info.scissorCount = 1;
	viewport_state_creation_info.pViewports = &dummy_viewport;
    viewport_state_creation_info.pScissors = &dummy_scissor;

    VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_state_creation_info = {0};
    dynamic_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_creation_info.dynamicStateCount = (uint32)(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
    dynamic_state_creation_info.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo rasterization_state_creation_info = {0};
    rasterization_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_creation_info.depthClampEnable = VK_FALSE;
    rasterization_state_creation_info.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state_creation_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state_creation_info.cullMode = VK_CULL_MODE_NONE;
    rasterization_state_creation_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_creation_info.depthBiasEnable = VK_FALSE; 
    rasterization_state_creation_info.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state_creation_info = {0};
    multisample_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_creation_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; 
    multisample_state_creation_info.sampleShadingEnable = VK_FALSE;
    multisample_state_creation_info.minSampleShading = 1.0f;
    multisample_state_creation_info.pSampleMask = 0;
    multisample_state_creation_info.alphaToCoverageEnable = VK_FALSE;
    multisample_state_creation_info.alphaToOneEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo reflection_multisample = multisample_state_creation_info;
    reflection_multisample.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

	// shared info for graphics pipeline that is tweaked based on sprite or cube (default right now is cube, but the parts that are particular to cube are defined later anyway)

	VkPipelineColorBlendAttachmentState color_blend_attachment_state = {0};
    VkPipelineColorBlendAttachmentState blend_attachments[2] = {0};
    color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment_state.blendEnable = VK_FALSE;
    color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo color_blend_state_creation_info = {0}; // pipeline-level color blend state (not blending, all fields are pretty much default)
    color_blend_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_creation_info.logicOpEnable = VK_FALSE;
    color_blend_state_creation_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_creation_info.attachmentCount = 2;
    color_blend_state_creation_info.pAttachments = blend_attachments;
    color_blend_state_creation_info.blendConstants[0] = 0.0f;
    color_blend_state_creation_info.blendConstants[1] = 0.0f;
    color_blend_state_creation_info.blendConstants[2] = 0.0f;
    color_blend_state_creation_info.blendConstants[3] = 0.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_creation_info = {0}; // depth/stencil settings for the pipeline (not using this for simple 2D)
	depth_stencil_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil_state_creation_info.depthTestEnable =  VK_TRUE;
	depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
	depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;
	depth_stencil_state_creation_info.depthBoundsTestEnable = VK_FALSE;
	depth_stencil_state_creation_info.stencilTestEnable = VK_TRUE;
    depth_stencil_state_creation_info.front.failOp = VK_STENCIL_OP_KEEP;
    depth_stencil_state_creation_info.front.passOp = VK_STENCIL_OP_REPLACE;
    depth_stencil_state_creation_info.front.depthFailOp = VK_STENCIL_OP_KEEP;
    depth_stencil_state_creation_info.front.compareOp = VK_COMPARE_OP_ALWAYS;
    depth_stencil_state_creation_info.front.compareMask = 0xFF;
    depth_stencil_state_creation_info.front.writeMask = 0xFF;
    depth_stencil_state_creation_info.front.reference = 1;
    depth_stencil_state_creation_info.back = depth_stencil_state_creation_info.front;

	// descriptors + pipeline layout (shared by sprite and cube pipelines)

    // pixel art sampler
    {
        VkSamplerCreateInfo pixel_sampler_ci = {0};
        pixel_sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        pixel_sampler_ci.magFilter = VK_FILTER_NEAREST;
        pixel_sampler_ci.minFilter = VK_FILTER_NEAREST;
        pixel_sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pixel_sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pixel_sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        pixel_sampler_ci.anisotropyEnable = VK_FALSE;
        pixel_sampler_ci.maxAnisotropy = 1.0f;
        pixel_sampler_ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        pixel_sampler_ci.unnormalizedCoordinates = VK_FALSE;
        pixel_sampler_ci.compareEnable = VK_FALSE;
        pixel_sampler_ci.compareOp = VK_COMPARE_OP_ALWAYS;
        pixel_sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        pixel_sampler_ci.mipLodBias = 0.0f;
        pixel_sampler_ci.minLod = 0.0f;
        pixel_sampler_ci.maxLod = 0.0f;

        vkCreateSampler(vulkan_state.logical_device_handle, &pixel_sampler_ci, 0, &vulkan_state.pixel_art_sampler);
    }

    // linear clamp sampler
    {
        VkSamplerCreateInfo linear_clamp_ci = {0};
        linear_clamp_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        linear_clamp_ci.magFilter = VK_FILTER_LINEAR;
        linear_clamp_ci.minFilter = VK_FILTER_LINEAR;
        linear_clamp_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linear_clamp_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linear_clamp_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linear_clamp_ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        linear_clamp_ci.unnormalizedCoordinates = VK_FALSE;
        linear_clamp_ci.compareEnable = VK_FALSE;
        linear_clamp_ci.compareOp = VK_COMPARE_OP_ALWAYS;
        linear_clamp_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        linear_clamp_ci.maxLod = 0.0f;

        vkCreateSampler(vulkan_state.logical_device_handle, &linear_clamp_ci, 0, &vulkan_state.linear_clamp_sampler);
    }

    // linear sampler with repeat
    {
        VkSamplerCreateInfo linear_sampler_ci = {0};
        linear_sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        linear_sampler_ci.magFilter = VK_FILTER_LINEAR;
        linear_sampler_ci.minFilter = VK_FILTER_LINEAR;
        linear_sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_sampler_ci.anisotropyEnable = VK_FALSE;
        linear_sampler_ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        linear_sampler_ci.unnormalizedCoordinates = VK_FALSE;
        linear_sampler_ci.compareEnable = VK_FALSE;
        linear_sampler_ci.compareOp = VK_COMPARE_OP_ALWAYS;
        linear_sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        linear_sampler_ci.minLod = 0.0f;
        linear_sampler_ci.maxLod = VK_LOD_CLAMP_NONE;
        linear_sampler_ci.mipLodBias = 0.0f;
        linear_sampler_ci.anisotropyEnable = VK_TRUE;
        linear_sampler_ci.maxAnisotropy = 16.0f;
        
        vkCreateSampler(vulkan_state.logical_device_handle, &linear_sampler_ci, 0, &vulkan_state.tiling_linear_sampler);
    }

    // shadow sampler
    {
        VkSamplerCreateInfo shadow_sampler_ci = {0};
        shadow_sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        shadow_sampler_ci.magFilter = VK_FILTER_LINEAR;
        shadow_sampler_ci.minFilter = VK_FILTER_LINEAR;
        shadow_sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; // 1.0 on border, means no shadows here
        shadow_sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        shadow_sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        shadow_sampler_ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        shadow_sampler_ci.anisotropyEnable = VK_FALSE;
        shadow_sampler_ci.maxAnisotropy = 1.0f;
        shadow_sampler_ci.unnormalizedCoordinates = VK_FALSE;
        shadow_sampler_ci.compareEnable = VK_TRUE;
        shadow_sampler_ci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        shadow_sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        shadow_sampler_ci.minLod = 0.0f;
        shadow_sampler_ci.maxLod = 0.0f;

        vkCreateSampler(vulkan_state.logical_device_handle, &shadow_sampler_ci, 0, &vulkan_state.shadow_sampler);
    }

    // standard descriptor set for normal vertex / fragment shader pairs
	VkDescriptorSetLayoutBinding descriptor_set_binding = {0};
	descriptor_set_binding.binding = 0;
    descriptor_set_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_set_binding.descriptorCount = 1;
    descriptor_set_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_creation_info = {0};
    descriptor_set_layout_creation_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_creation_info.bindingCount = 1;
    descriptor_set_layout_creation_info.pBindings = &descriptor_set_binding;

    vkCreateDescriptorSetLayout(vulkan_state.logical_device_handle, &descriptor_set_layout_creation_info, 0, &vulkan_state.descriptor_set_layout);

    // ssbo layout TODO: comments / naming these 3 better
    VkDescriptorSetLayoutBinding ssbo_binding = {0};
    ssbo_binding.binding = 0;
    ssbo_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssbo_binding.descriptorCount = 1;
    ssbo_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ssbo_layout_ci = {0};
    ssbo_layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ssbo_layout_ci.bindingCount = 1;
    ssbo_layout_ci.pBindings = &ssbo_binding;

    vkCreateDescriptorSetLayout(vulkan_state.logical_device_handle, &ssbo_layout_ci, 0, &vulkan_state.ssbo_descriptor_set_layout);

    VkDescriptorSetLayoutBinding storage_image_binding = {0};
    storage_image_binding.binding = 0;
    storage_image_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storage_image_binding.descriptorCount = 1;
    storage_image_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo storage_image_layout_ci = {0};
    storage_image_layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    storage_image_layout_ci.bindingCount = 1;
    storage_image_layout_ci.pBindings = &storage_image_binding;

    vkCreateDescriptorSetLayout(vulkan_state.logical_device_handle, &storage_image_layout_ci, 0, &vulkan_state.storage_image_descriptor_set_layout);

    // descriptor pool allocates memory for all descriptor sets
    VkDescriptorPoolSize descriptor_pool_sizes[4] = {0};
   	descriptor_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_pool_sizes[0].descriptorCount = 1024;
    descriptor_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptor_pool_sizes[1].descriptorCount = 4;
    descriptor_pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptor_pool_sizes[2].descriptorCount = 4 + 1 + 1 + 1 + 1 + 1; // oit head image, h0, fft_buffer_a and b, water displacement TODO: this is silly
    descriptor_pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptor_pool_sizes[3].descriptorCount = 8 + 2; // +2 for oit pool and counter
    
    VkDescriptorPoolCreateInfo descriptor_pool_creation_info = {0};
    descriptor_pool_creation_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_creation_info.poolSizeCount = 4;
    descriptor_pool_creation_info.pPoolSizes = descriptor_pool_sizes;
    descriptor_pool_creation_info.maxSets = 1024;

    vkCreateDescriptorPool(vulkan_state.logical_device_handle, &descriptor_pool_creation_info, 0, &vulkan_state.descriptor_pool);

    // VIEW CONSTANTS UBO (per frame in flight)
    {
        VkPhysicalDeviceProperties props = {0};
        vkGetPhysicalDeviceProperties(vulkan_state.physical_device_handle, &props);
        VkDeviceSize align = props.limits.minUniformBufferOffsetAlignment;
        vulkan_state.view_constants_stride = (uint32)((sizeof(ViewConstants) + align - 1) & ~(align - 1));

        // descriptor set layout: one dynamic ubo
        VkDescriptorSetLayoutBinding binding = {0};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_ci.bindingCount = 1;
        layout_ci.pBindings = &binding;
        vkCreateDescriptorSetLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.view_constants_set_layout);

        VkDeviceSize buffer_size = (VkDeviceSize)vulkan_state.view_constants_stride * VIEW_BLOCK_COUNT;

        for (int in_flight_index = 0; in_flight_index < 2; in_flight_index++)
        {
            VkBufferCreateInfo buffer_ci = {0};
            buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_ci.size = buffer_size;
            buffer_ci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(vulkan_state.logical_device_handle, &buffer_ci, 0, &vulkan_state.scene_ubo_buffers[in_flight_index]);

            VkMemoryRequirements mem_req = {0};
            vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.scene_ubo_buffers[in_flight_index], &mem_req);

            VkMemoryAllocateInfo alloc = {0};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize = mem_req.size;
            alloc.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(vulkan_state.logical_device_handle, &alloc, 0, &vulkan_state.scene_ubo_memories[in_flight_index]);
            vkBindBufferMemory(vulkan_state.logical_device_handle, vulkan_state.scene_ubo_buffers[in_flight_index], vulkan_state.scene_ubo_memories[in_flight_index], 0);

            vkMapMemory(vulkan_state.logical_device_handle, vulkan_state.scene_ubo_memories[in_flight_index], 0, buffer_size, 0, &vulkan_state.scene_ubo_mappeds[in_flight_index]);

            VkDescriptorSetAllocateInfo ds_alloc = {0};
            ds_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ds_alloc.descriptorPool = vulkan_state.descriptor_pool;
            ds_alloc.descriptorSetCount = 1;
            ds_alloc.pSetLayouts = &vulkan_state.view_constants_set_layout;
            vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &ds_alloc, &vulkan_state.view_constants_descriptor_sets[in_flight_index]);

            VkDescriptorBufferInfo buffer_info = {0};
            buffer_info.buffer = vulkan_state.scene_ubo_buffers[in_flight_index];
            buffer_info.offset = 0;
            buffer_info.range = sizeof(ViewConstants);

            VkWriteDescriptorSet write_ds = {0};
            write_ds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_ds.dstSet = vulkan_state.view_constants_descriptor_sets[in_flight_index];
            write_ds.dstBinding = 0;
            write_ds.descriptorCount = 1;
            write_ds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            write_ds.pBufferInfo = &buffer_info;
            vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write_ds, 0, 0);
        }
    }

    VkDescriptorSetAllocateInfo depth_descriptor_set_alloc = {0};
    depth_descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    depth_descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
    depth_descriptor_set_alloc.descriptorSetCount = 1;
    depth_descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &depth_descriptor_set_alloc, &vulkan_state.depth_descriptor_set);

	VkDescriptorSetAllocateInfo normal_descriptor_set_alloc = {0};
    normal_descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    normal_descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
    normal_descriptor_set_alloc.descriptorSetCount = 1;
    normal_descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &normal_descriptor_set_alloc, &vulkan_state.normal_descriptor_set);

    VkDescriptorSetAllocateInfo scene_copy_descriptor_set_alloc = {0};
    scene_copy_descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    scene_copy_descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
    scene_copy_descriptor_set_alloc.descriptorSetCount = 1;
    scene_copy_descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &scene_copy_descriptor_set_alloc, &vulkan_state.scene_copy_descriptor_set);

    VkDescriptorSetAllocateInfo water_depth_descriptor_set_alloc = {0};
    water_depth_descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    water_depth_descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
    water_depth_descriptor_set_alloc.descriptorSetCount = 1;
    water_depth_descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &water_depth_descriptor_set_alloc, &vulkan_state.water_depth_descriptor_set);

    // reflection descriptor set
    VkDescriptorSetAllocateInfo reflection_descriptor_set_alloc = {0};
    reflection_descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    reflection_descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
    reflection_descriptor_set_alloc.descriptorSetCount = 1;
    reflection_descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &reflection_descriptor_set_alloc, &vulkan_state.reflection_descriptor_set);

    // OIT head image descriptor set
    VkDescriptorSetAllocateInfo oit_head_alloc_info = {0};
    oit_head_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    oit_head_alloc_info.descriptorPool = vulkan_state.descriptor_pool;
    oit_head_alloc_info.descriptorSetCount = 1;
    oit_head_alloc_info.pSetLayouts = &vulkan_state.storage_image_descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &oit_head_alloc_info, &vulkan_state.oit_head_storage_descriptor_set);

    // OIT fragment pool descriptor set
    VkDescriptorSetAllocateInfo oit_pool_alloc_info = {0};
    oit_pool_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    oit_pool_alloc_info.descriptorPool = vulkan_state.descriptor_pool;
    oit_pool_alloc_info.descriptorSetCount = 1;
    oit_pool_alloc_info.pSetLayouts = &vulkan_state.ssbo_descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &oit_pool_alloc_info, &vulkan_state.oit_fragment_pool_descriptor_set);

    // OIT counter descriptor set
    VkDescriptorSetAllocateInfo oit_counter_alloc_info = {0};
    oit_counter_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    oit_counter_alloc_info.descriptorPool = vulkan_state.descriptor_pool;
    oit_counter_alloc_info.descriptorSetCount = 1;
    oit_counter_alloc_info.pSetLayouts = &vulkan_state.ssbo_descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &oit_counter_alloc_info, &vulkan_state.oit_counter_descriptor_set);
    
    // below descriptor sets are updated only once, because they don't need to be updated on resize

    // shadow map descriptor set
    {
        VkDescriptorSetAllocateInfo shadow_map_descriptor_set_alloc = {0};
        shadow_map_descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        shadow_map_descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
        shadow_map_descriptor_set_alloc.descriptorSetCount = 1;
        shadow_map_descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &shadow_map_descriptor_set_alloc, &vulkan_state.shadow_map_descriptor_set);

        VkDescriptorImageInfo shadow_map_image_info = {0};
        shadow_map_image_info.sampler = vulkan_state.shadow_sampler;
        shadow_map_image_info.imageView = vulkan_state.shadow_map_image_view;
        shadow_map_image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet shadow_map_descriptor_write = {0};
        shadow_map_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadow_map_descriptor_write.dstSet = vulkan_state.shadow_map_descriptor_set;
        shadow_map_descriptor_write.dstBinding = 0;
        shadow_map_descriptor_write.descriptorCount = 1;
        shadow_map_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadow_map_descriptor_write.pImageInfo = &shadow_map_image_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &shadow_map_descriptor_write, 0, 0);
    }

    // h0 storage image descriptor set
    {
        VkDescriptorSetAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vulkan_state.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &vulkan_state.storage_image_descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &alloc_info, &vulkan_state.h0_descriptor_set);

        VkDescriptorImageInfo image_info = {0};
        image_info.imageView = vulkan_state.h0_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.h0_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // fft buffer a and b storage
    {
        VkDescriptorSetAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vulkan_state.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &vulkan_state.storage_image_descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &alloc_info, &vulkan_state.fft_buffer_a_descriptor_set);

        VkDescriptorImageInfo image_info = {0};
        image_info.imageView = vulkan_state.fft_buffer_a_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.fft_buffer_a_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // fft_buffer_a sampled
    {
        VkDescriptorSetAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vulkan_state.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &vulkan_state.descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &alloc_info, &vulkan_state.fft_buffer_a_sampled_descriptor_set);

        VkDescriptorImageInfo image_info = {0};
        image_info.sampler = vulkan_state.pixel_art_sampler;
        image_info.imageView = vulkan_state.fft_buffer_a_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.fft_buffer_a_sampled_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // fft_buffer_b storage TODO: smarter way to do two below, duplicated code here
    {
        VkDescriptorSetAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vulkan_state.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &vulkan_state.storage_image_descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &alloc_info, &vulkan_state.fft_buffer_b_descriptor_set);

        VkDescriptorImageInfo image_info = {0};
        image_info.imageView = vulkan_state.fft_buffer_b_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.fft_buffer_b_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // fft_buffer_b sampled
    {
        VkDescriptorSetAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vulkan_state.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &vulkan_state.descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &alloc_info, &vulkan_state.fft_buffer_b_sampled_descriptor_set);

        VkDescriptorImageInfo image_info = {0};
        image_info.sampler = vulkan_state.pixel_art_sampler;
        image_info.imageView = vulkan_state.fft_buffer_b_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.fft_buffer_b_sampled_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // displacement storage
    {
        VkDescriptorSetAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vulkan_state.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &vulkan_state.storage_image_descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &alloc_info, &vulkan_state.displacement_descriptor_set);
        
        VkDescriptorImageInfo image_info = {0};
        image_info.imageView = vulkan_state.displacement_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.displacement_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image_info;
        
        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

    // displacement sampled
    {
        VkDescriptorSetAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vulkan_state.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &vulkan_state.descriptor_set_layout;
        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &alloc_info, &vulkan_state.displacement_sampled_descriptor_set);
        
        VkDescriptorImageInfo image_info = {0};
        image_info.sampler = vulkan_state.tiling_linear_sampler;
        image_info.imageView = vulkan_state.displacement_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = vulkan_state.displacement_sampled_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        
        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &write, 0, 0);
    }

	createSwapchainResources();

	// CUBE (INSTANCED) PIPELINE LAYOUT
    {
        VkDescriptorSetLayout cube_set_layouts[4] =
        {
            vulkan_state.view_constants_set_layout,
            vulkan_state.descriptor_set_layout, // atlas
            vulkan_state.descriptor_set_layout, // water displacement
            vulkan_state.descriptor_set_layout, // shadow map
        };
        VkPipelineLayoutCreateInfo layout_info = {0};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 4;
        layout_info.pSetLayouts = cube_set_layouts;
        layout_info.pushConstantRangeCount = 0;
        layout_info.pPushConstantRanges = 0;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_info, 0, &vulkan_state.cube_pipeline_layout);
    }

    // MODEL PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(ModelPushConstants);

        VkDescriptorSetLayout model_set_layouts[3] =
        {
            vulkan_state.view_constants_set_layout, // per-view constants
            vulkan_state.descriptor_set_layout,     // water displacement
            vulkan_state.descriptor_set_layout,     // shadow map
        };

        VkPipelineLayoutCreateInfo model_pipeline_layout_ci = {0};
        model_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        model_pipeline_layout_ci.setLayoutCount = 3;
        model_pipeline_layout_ci.pSetLayouts = model_set_layouts;
        model_pipeline_layout_ci.pushConstantRangeCount = 1;
        model_pipeline_layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &model_pipeline_layout_ci, 0, &vulkan_state.model_pipeline_layout);
    }

    // OUTLINE POST PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(float) * 6; // texel_size, depth threshold, normal_threshold, experimental shaders

        VkDescriptorSetLayout post_layouts[2] = 
        {
            vulkan_state.descriptor_set_layout, 
            vulkan_state.descriptor_set_layout,
        };

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 2;
        layout_ci.pSetLayouts = post_layouts;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.outline_post_pipeline_layout);
    }

    // WATER PIPELINE LAYOUT
    {
        VkDescriptorSetLayout water_set_layouts[8] =
        {
            vulkan_state.view_constants_set_layout, // view constants
            vulkan_state.descriptor_set_layout,     // underwater scene copy
            vulkan_state.descriptor_set_layout,     // scene depth
            vulkan_state.descriptor_set_layout,     // paint texture
            vulkan_state.descriptor_set_layout,     // displacement texture
            vulkan_state.descriptor_set_layout,     // reflection texture
            vulkan_state.descriptor_set_layout,     // water grid texture
            vulkan_state.descriptor_set_layout,     // water grid normals texture
        };

        VkPipelineLayoutCreateInfo water_pipeline_layout_ci = {0};
        water_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        water_pipeline_layout_ci.setLayoutCount = 8;
        water_pipeline_layout_ci.pSetLayouts = water_set_layouts;
        water_pipeline_layout_ci.pushConstantRangeCount = 0;
        water_pipeline_layout_ci.pPushConstantRanges = 0;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &water_pipeline_layout_ci, 0, &vulkan_state.water_pipeline_layout);
    }


    // WATERLINE PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(float) * 4;

        VkDescriptorSetLayout waterline_set_layouts[2] = 
        {
            vulkan_state.descriptor_set_layout, // scene depth
            vulkan_state.descriptor_set_layout, // water depth
        };

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 2;
        layout_ci.pSetLayouts = waterline_set_layouts;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;
        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.waterline_pipeline_layout);
    }

    // EDITOR OUTLINE PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(OutlinePushConstants);

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &vulkan_state.view_constants_set_layout;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.editor_outline_pipeline_layout);
    }

    // OIT LASER WRITE PIPELINE LAYOUT
    {
        VkDescriptorSetLayout laser_set_layouts[4] = 
        {
            vulkan_state.view_constants_set_layout,            // view constants
            vulkan_state.storage_image_descriptor_set_layout,  // head image
            vulkan_state.ssbo_descriptor_set_layout,           // fragment pool
            vulkan_state.ssbo_descriptor_set_layout,           // counter
        };

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 4;
        layout_ci.pSetLayouts = laser_set_layouts;
        layout_ci.pushConstantRangeCount = 0;
        layout_ci.pPushConstantRanges = 0;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.laser_pipeline_layout);
    }

    // OIT RESOLVE PIPELINE LAYOUT
    {
        VkDescriptorSetLayout oit_resolve_set_layouts[4] = 
        {
            vulkan_state.storage_image_descriptor_set_layout,  // head image
            vulkan_state.ssbo_descriptor_set_layout,           // fragment pool
            vulkan_state.ssbo_descriptor_set_layout,           // counter (not really needed here)
            vulkan_state.descriptor_set_layout,				   // depth sampler
        };

        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(float); // depth_threshold

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 4;
        layout_ci.pSetLayouts = oit_resolve_set_layouts;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.oit_resolve_pipeline_layout);
    }

	// SPRITE PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(SpritePushConstants);

        VkDescriptorSetLayout sprite_set_layouts[2] =
        {
            vulkan_state.view_constants_set_layout, // per-view constants
            vulkan_state.descriptor_set_layout,     // sprite atlas texture
        };

        VkPipelineLayoutCreateInfo sprite_pipeline_layout_ci = {0};
        sprite_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        sprite_pipeline_layout_ci.setLayoutCount = 2;
        sprite_pipeline_layout_ci.pSetLayouts = sprite_set_layouts;
        sprite_pipeline_layout_ci.pushConstantRangeCount = 1;
        sprite_pipeline_layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &sprite_pipeline_layout_ci, 0, &vulkan_state.sprite_pipeline_layout);
    }

    // SHADOW PIPELINE LAYOUT
    {
        VkPushConstantRange shadow_push_constant_range = {0};
        shadow_push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        shadow_push_constant_range.offset = 0;
        shadow_push_constant_range.size = sizeof(float) * 16;

        VkPipelineLayoutCreateInfo shadow_pipeline_layout_ci = {0};
        shadow_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        shadow_pipeline_layout_ci.setLayoutCount = 1;
        shadow_pipeline_layout_ci.pSetLayouts = &vulkan_state.view_constants_set_layout;
        shadow_pipeline_layout_ci.pushConstantRangeCount = 1;
        shadow_pipeline_layout_ci.pPushConstantRanges = &shadow_push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &shadow_pipeline_layout_ci, 0, &vulkan_state.shadow_pipeline_layout);
    }

    // FFT SPECTRUM PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(FFTSpectrumPushConstants);

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &vulkan_state.storage_image_descriptor_set_layout;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.fft_spectrum_pipeline_layout);
    }


    // FFT EVOLVED PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(FFTEvolvedPushConstants);

        VkDescriptorSetLayout evolved_set_layouts[2] =
        {
            vulkan_state.storage_image_descriptor_set_layout, // h0 read
            vulkan_state.storage_image_descriptor_set_layout, // fft_buffer_a write
        };

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 2;
        layout_ci.pSetLayouts = evolved_set_layouts;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.fft_evolved_pipeline_layout);
    }

    // FFT PASS PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(FFTPassPushConstants);

        VkDescriptorSetLayout fft_pass_set_layouts[2] =
        {
            vulkan_state.storage_image_descriptor_set_layout,
            vulkan_state.storage_image_descriptor_set_layout,
        };

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 2;
        layout_ci.pSetLayouts = fft_pass_set_layouts;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.fft_pass_pipeline_layout);
    }

    // FFT FINALIZE PIPELINE LAYOUT
    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(FFTFinalizePushConstants);
        
        VkDescriptorSetLayout finalize_set_layouts[2] =
        {
            vulkan_state.storage_image_descriptor_set_layout, // read from fft_buffer_a
            vulkan_state.storage_image_descriptor_set_layout, // write to displacement
        };
        
        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 2;
        layout_ci.pSetLayouts = finalize_set_layouts;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;
        
        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.fft_finalize_pipeline_layout);
    }

    // BASE GRAPHICS PIPELINE INFO

   	VkGraphicsPipelineCreateInfo base_graphics_pipeline_creation_info = {0}; // struct that points to all those sub-blocks we just defined; it actually builds the pipeline object
	base_graphics_pipeline_creation_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	base_graphics_pipeline_creation_info.stageCount = 2;
	base_graphics_pipeline_creation_info.pStages = cube_shader_stages;
	base_graphics_pipeline_creation_info.pVertexInputState = &vertex_input_simple;
	base_graphics_pipeline_creation_info.pInputAssemblyState = &input_assembly_state_creation_info;
	base_graphics_pipeline_creation_info.pViewportState = &viewport_state_creation_info;
	base_graphics_pipeline_creation_info.pRasterizationState = &rasterization_state_creation_info; // fill mode, cull off
	base_graphics_pipeline_creation_info.pMultisampleState = &multisample_state_creation_info; // multisampling disabled (1x MSAA)
	base_graphics_pipeline_creation_info.pDepthStencilState = &depth_stencil_state_creation_info;
	base_graphics_pipeline_creation_info.pColorBlendState = &color_blend_state_creation_info; // one color attachment; no blending
	base_graphics_pipeline_creation_info.pDynamicState = &dynamic_state_creation_info;
	base_graphics_pipeline_creation_info.layout = 0; // will be set by each individual pipeline
	base_graphics_pipeline_creation_info.renderPass = vulkan_state.render_pass_handle;
	base_graphics_pipeline_creation_info.subpass = 0; // first (and only) subpass
	base_graphics_pipeline_creation_info.basePipelineHandle = VK_NULL_HANDLE; // not deriving from another pipeline.
	base_graphics_pipeline_creation_info.basePipelineIndex = -1;

    vulkan_state.atlas_2d_asset_index          = getOrLoadAsset((char*)ATLAS_2D_PATH,          VK_FORMAT_R8G8B8A8_SRGB);
    vulkan_state.atlas_font_asset_index        = getOrLoadAsset((char*)ATLAS_FONT_PATH,        VK_FORMAT_R8G8B8A8_SRGB);
    vulkan_state.atlas_3d_asset_index          = getOrLoadAsset((char*)ATLAS_3D_PATH,          VK_FORMAT_R8G8B8A8_SRGB);

    vulkan_state.water_grid_asset_index        = loadDdsArray((char*)WATER_GRID_PATH);
    vulkan_state.water_grid_normal_asset_index = loadDdsArray((char*)WATER_GRID_NORMAL_PATH);

	// define instanced cube pipeline: depth on, blending off
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        blend_attachments[0] = color_blend_attachment_state;
        blend_attachments[1] = color_blend_attachment_state;

        VkGraphicsPipelineCreateInfo cube_ci = base_graphics_pipeline_creation_info;
        cube_ci.pVertexInputState = &vertex_input_instanced;
        cube_ci.layout = vulkan_state.cube_pipeline_layout;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &cube_ci, 0, &vulkan_state.cube_pipeline);
    }

    // define cube pipeline for reflection
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo refl_blend_ci = {0};
        refl_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        refl_blend_ci.attachmentCount = 1;
        refl_blend_ci.pAttachments = &color_blend_attachment_state;

        VkGraphicsPipelineCreateInfo ci = base_graphics_pipeline_creation_info;
        ci.pVertexInputState = &vertex_input_instanced;
        ci.layout = vulkan_state.cube_pipeline_layout;
        ci.renderPass = vulkan_state.reflection_render_pass;
        ci.pColorBlendState = &refl_blend_ci;
        ci.pMultisampleState = &reflection_multisample;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &ci, 0, &vulkan_state.cube_reflection_pipeline);
    }

    // define model pipeline: depth on, write to stencil 2.
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        blend_attachments[0] = color_blend_attachment_state;
        blend_attachments[1] = color_blend_attachment_state;

        VkGraphicsPipelineCreateInfo model_ci = base_graphics_pipeline_creation_info;
        model_ci.pStages = model_shader_stages;
        model_ci.layout = vulkan_state.model_pipeline_layout;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &model_ci, 0, &vulkan_state.model_pipeline);
    }

    // define model pipeline for reflection
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo refl_blend_ci = {0};
        refl_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        refl_blend_ci.attachmentCount = 1;
        refl_blend_ci.pAttachments = &color_blend_attachment_state;

        VkGraphicsPipelineCreateInfo model_ci = base_graphics_pipeline_creation_info;
        model_ci.pStages = model_shader_stages;
        model_ci.layout = vulkan_state.model_pipeline_layout;
        model_ci.renderPass = vulkan_state.reflection_render_pass;
        model_ci.pColorBlendState = &refl_blend_ci;
        model_ci.pMultisampleState = &reflection_multisample;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &model_ci, 0, &vulkan_state.model_reflection_pipeline);
    }

    // define overlay outline pipeline (for drawing selection outlines on top of everything)
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo overlay_outline_blend_ci = {0};
        overlay_outline_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        overlay_outline_blend_ci.attachmentCount = 1;
        overlay_outline_blend_ci.pAttachments = &color_blend_attachment_state;

        depth_stencil_state_creation_info.depthTestEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;

        rasterization_state_creation_info.polygonMode = VK_POLYGON_MODE_LINE;
        rasterization_state_creation_info.cullMode = VK_CULL_MODE_NONE;
        rasterization_state_creation_info.lineWidth = 1.0f;

        VkGraphicsPipelineCreateInfo overlay_outline_ci = base_graphics_pipeline_creation_info;
        overlay_outline_ci.pStages = outline_select_shader_stages;
        overlay_outline_ci.layout = vulkan_state.editor_outline_pipeline_layout;
        overlay_outline_ci.renderPass = vulkan_state.overlay_render_pass;
        overlay_outline_ci.pColorBlendState = &overlay_outline_blend_ci;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &overlay_outline_ci, 0, &vulkan_state.editor_outline_pipeline);
    }

    // define water pipeline (merged depth and real water pass)
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        depth_stencil_state_creation_info.depthTestEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.stencilTestEnable = VK_FALSE;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterization_state_creation_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineColorBlendAttachmentState water_blend_attachments[2] = {0};

        water_blend_attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        water_blend_attachments[0].blendEnable = VK_TRUE;
        water_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        water_blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        water_blend_attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        water_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        water_blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        water_blend_attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

        water_blend_attachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        water_blend_attachments[1].blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo water_blend_ci = {0};
        water_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        water_blend_ci.attachmentCount = 2;
        water_blend_ci.pAttachments = water_blend_attachments;

        VkGraphicsPipelineCreateInfo water_pipeline_ci = base_graphics_pipeline_creation_info;
        water_pipeline_ci.pVertexInputState = &water_vertex_input;
        water_pipeline_ci.pStages = water_shader_stages;
        water_pipeline_ci.layout = vulkan_state.water_pipeline_layout;
        water_pipeline_ci.renderPass = vulkan_state.water_render_pass;
        water_pipeline_ci.pColorBlendState = &water_blend_ci;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &water_pipeline_ci, 0, &vulkan_state.water_pipeline);
    }

    // define waterline pipeline
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        depth_stencil_state_creation_info.depthTestEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.stencilTestEnable = VK_FALSE;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_NONE;

        VkPipelineVertexInputStateCreateInfo waterline_vertex_input = {0};
        waterline_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineColorBlendAttachmentState waterline_blend = {0};
        waterline_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        waterline_blend.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo waterline_blend_ci = {0};
        waterline_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        waterline_blend_ci.attachmentCount = 1;
        waterline_blend_ci.pAttachments = &waterline_blend;

        VkGraphicsPipelineCreateInfo waterline_pipeline_ci = base_graphics_pipeline_creation_info;
        waterline_pipeline_ci.pVertexInputState = &waterline_vertex_input;
        waterline_pipeline_ci.pStages = waterline_shader_stages;
        waterline_pipeline_ci.layout = vulkan_state.waterline_pipeline_layout;
        waterline_pipeline_ci.renderPass = vulkan_state.waterline_render_pass;
        waterline_pipeline_ci.pColorBlendState = &waterline_blend_ci;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &waterline_pipeline_ci, 0, &vulkan_state.waterline_pipeline);
    }

    // shadow cube pipeline
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil_state_creation_info.stencilTestEnable = VK_FALSE;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterization_state_creation_info.depthBiasEnable = VK_TRUE;
        rasterization_state_creation_info.depthBiasConstantFactor = 1.0f;
        rasterization_state_creation_info.depthBiasSlopeFactor = 2.0f;

        VkPipelineColorBlendStateCreateInfo shadow_blend_ci = {0};
        shadow_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        shadow_blend_ci.attachmentCount = 0;
        shadow_blend_ci.pAttachments = 0;

        VkGraphicsPipelineCreateInfo shadow_cube_ci = base_graphics_pipeline_creation_info;
        shadow_cube_ci.stageCount = 1;
        shadow_cube_ci.pStages = shadow_cube_stages;
        shadow_cube_ci.pVertexInputState = &vertex_input_instanced;
        shadow_cube_ci.pColorBlendState = &shadow_blend_ci;
        shadow_cube_ci.layout = vulkan_state.shadow_pipeline_layout;
        shadow_cube_ci.renderPass = vulkan_state.shadow_render_pass;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &shadow_cube_ci, 0, &vulkan_state.shadow_cube_pipeline);
    }

    // shadow model pipeline
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil_state_creation_info.stencilTestEnable = VK_FALSE;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_FRONT_BIT;
        rasterization_state_creation_info.depthBiasEnable = VK_TRUE;
        rasterization_state_creation_info.depthBiasConstantFactor = 1.0f;
        rasterization_state_creation_info.depthBiasSlopeFactor = 2.0f;

        VkPipelineColorBlendStateCreateInfo shadow_blend_ci = {0};
        shadow_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        shadow_blend_ci.attachmentCount = 0;
        shadow_blend_ci.pAttachments = 0;

        VkGraphicsPipelineCreateInfo shadow_model_ci = base_graphics_pipeline_creation_info;
        shadow_model_ci.stageCount = 1;
        shadow_model_ci.pStages = shadow_model_stages;
        shadow_model_ci.pVertexInputState = &vertex_input_simple;
        shadow_model_ci.pColorBlendState = &shadow_blend_ci;
        shadow_model_ci.layout = vulkan_state.shadow_pipeline_layout;
        shadow_model_ci.renderPass = vulkan_state.shadow_render_pass;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &shadow_model_ci, 0, &vulkan_state.shadow_model_pipeline);
    }

    // define outline post pipeline. different enough that we might as well set up an entirely new creation info. sets up state first, then assigns
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        VkPipelineInputAssemblyStateCreateInfo post_input_assembly_state_ci = {0};
        post_input_assembly_state_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        post_input_assembly_state_ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo post_viewport_state_ci = {0};
        post_viewport_state_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        post_viewport_state_ci.viewportCount = 1;
        post_viewport_state_ci.scissorCount = 1;
        post_viewport_state_ci.pViewports = &dummy_viewport;
        post_viewport_state_ci.pScissors = &dummy_scissor;

        VkDynamicState post_dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo post_dynamic_state_ci = {0};
        post_dynamic_state_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        post_dynamic_state_ci.dynamicStateCount = 2;
        post_dynamic_state_ci.pDynamicStates = post_dynamic_states;

        VkPipelineRasterizationStateCreateInfo post_rasterization_state_ci = {0};
        post_rasterization_state_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        post_rasterization_state_ci.polygonMode = VK_POLYGON_MODE_FILL;
        post_rasterization_state_ci.cullMode = VK_CULL_MODE_NONE;
        post_rasterization_state_ci.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo post_multisample_state_ci = {0};
        post_multisample_state_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        post_multisample_state_ci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState post_blend_attachment_state = {0};
        post_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        post_blend_attachment_state.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo post_color_blend_state_ci = {0};
        post_color_blend_state_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        post_color_blend_state_ci.attachmentCount = 1;
        post_color_blend_state_ci.pAttachments = &post_blend_attachment_state;

        VkPipelineDepthStencilStateCreateInfo post_depth_stencil_state_ci = {0};
        post_depth_stencil_state_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        post_depth_stencil_state_ci.depthTestEnable = VK_FALSE;
        post_depth_stencil_state_ci.depthWriteEnable = VK_FALSE;
        post_depth_stencil_state_ci.stencilTestEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo post_graphics_pipeline_ci = {0};
        post_graphics_pipeline_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        post_graphics_pipeline_ci.stageCount = 2;
        post_graphics_pipeline_ci.pStages = outline_post_shader_stages;
        post_graphics_pipeline_ci.pVertexInputState = &empty_vertex_input;
        post_graphics_pipeline_ci.pInputAssemblyState = &post_input_assembly_state_ci;
        post_graphics_pipeline_ci.pViewportState = &post_viewport_state_ci;
        post_graphics_pipeline_ci.pRasterizationState = &post_rasterization_state_ci;
        post_graphics_pipeline_ci.pMultisampleState = &post_multisample_state_ci;
        post_graphics_pipeline_ci.pDepthStencilState = &post_depth_stencil_state_ci;
        post_graphics_pipeline_ci.pColorBlendState = &post_color_blend_state_ci;
        post_graphics_pipeline_ci.pDynamicState = &post_dynamic_state_ci;
        post_graphics_pipeline_ci.layout = vulkan_state.outline_post_pipeline_layout;
        post_graphics_pipeline_ci.renderPass = vulkan_state.outline_post_render_pass;
        post_graphics_pipeline_ci.subpass = 0;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &post_graphics_pipeline_ci, 0, &vulkan_state.outline_post_pipeline);
    }

    // define laser pipeline
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        // disable blending, writing to the linked list not the color attachment
        color_blend_attachment_state.blendEnable = VK_FALSE;
        color_blend_attachment_state.colorWriteMask = 0;

        depth_stencil_state_creation_info.depthTestEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_BACK_BIT;

        VkPipelineColorBlendAttachmentState oit_blend = color_blend_attachment_state;
        VkPipelineColorBlendStateCreateInfo oit_blend_ci = {0};
        oit_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        oit_blend_ci.attachmentCount = 1;
        oit_blend_ci.pAttachments = &oit_blend;

        VkGraphicsPipelineCreateInfo laser_ci = base_graphics_pipeline_creation_info;
        laser_ci.pStages = laser_shader_stages;
        laser_ci.pVertexInputState = &laser_vertex_input;
        laser_ci.layout = vulkan_state.laser_pipeline_layout;
        laser_ci.renderPass = vulkan_state.overlay_render_pass;
        laser_ci.pColorBlendState = &oit_blend_ci;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &laser_ci, 0, &vulkan_state.laser_pipeline);
    }

    // define OIT resolve pipeline
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        VkPipelineColorBlendAttachmentState resolve_blend = {0};
        resolve_blend.blendEnable = VK_TRUE;
        resolve_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        resolve_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        resolve_blend.colorBlendOp = VK_BLEND_OP_ADD;
        resolve_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        resolve_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        resolve_blend.alphaBlendOp = VK_BLEND_OP_ADD;
        resolve_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo resolve_blend_ci = {0};
        resolve_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        resolve_blend_ci.attachmentCount = 1;
        resolve_blend_ci.pAttachments = &resolve_blend;

        VkPipelineDepthStencilStateCreateInfo resolve_depth = {0};
        resolve_depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        resolve_depth.depthTestEnable = VK_FALSE;
        resolve_depth.depthWriteEnable = VK_FALSE;
        resolve_depth.stencilTestEnable = VK_FALSE;

        VkPipelineRasterizationStateCreateInfo resolve_raster = {0};
        resolve_raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        resolve_raster.polygonMode = VK_POLYGON_MODE_FILL;
        resolve_raster.cullMode = VK_CULL_MODE_NONE;
        resolve_raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo resolve_multisample = {0};
        resolve_multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        resolve_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkGraphicsPipelineCreateInfo resolve_ci = {0};
        resolve_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        resolve_ci.stageCount = 2;
        resolve_ci.pStages = oit_resolve_shader_stages;
        resolve_ci.pVertexInputState = &empty_vertex_input;
        resolve_ci.pInputAssemblyState = &input_assembly_state_creation_info;
        resolve_ci.pViewportState = &viewport_state_creation_info;
        resolve_ci.pRasterizationState = &resolve_raster;
        resolve_ci.pMultisampleState = &resolve_multisample;
        resolve_ci.pDepthStencilState = &resolve_depth;
        resolve_ci.pColorBlendState = &resolve_blend_ci;
        resolve_ci.pDynamicState = &dynamic_state_creation_info;
        resolve_ci.layout = vulkan_state.oit_resolve_pipeline_layout;
        resolve_ci.renderPass = vulkan_state.overlay_render_pass;
        resolve_ci.subpass = 0;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &resolve_ci, 0, &vulkan_state.oit_resolve_pipeline);
    }

    // define sprite pipeline (overlay render pass)
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_TRUE;
        color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

        depth_stencil_state_creation_info.depthTestEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState sprite_blend = color_blend_attachment_state;
        VkPipelineColorBlendStateCreateInfo sprite_blend_ci = {0};
        sprite_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        sprite_blend_ci.attachmentCount = 1;
        sprite_blend_ci.pAttachments = &sprite_blend;

        VkGraphicsPipelineCreateInfo sprite_ci = base_graphics_pipeline_creation_info;
        sprite_ci.pStages = sprite_shader_stages;
        sprite_ci.layout = vulkan_state.sprite_pipeline_layout;
        sprite_ci.renderPass = vulkan_state.overlay_render_pass;
        sprite_ci.pColorBlendState = &sprite_blend_ci;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &sprite_ci, 0, &vulkan_state.sprite_pipeline);
    }

    // define FFT spectrum compute pipeline
    {
        VkComputePipelineCreateInfo pipeline_ci = {0};
        pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_ci.stage = fft_spectrum_stage_ci;
        pipeline_ci.layout = vulkan_state.fft_spectrum_pipeline_layout;

        vkCreateComputePipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &pipeline_ci, 0, &vulkan_state.fft_spectrum_pipeline);
    }

    // define FFT evolved compute pipeline
    {
        VkComputePipelineCreateInfo pipeline_ci = {0};
        pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_ci.stage = fft_evolved_stage_ci;
        pipeline_ci.layout = vulkan_state.fft_evolved_pipeline_layout;

        vkCreateComputePipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &pipeline_ci, 0, &vulkan_state.fft_evolved_pipeline);
    }

    // define FFT pass compute pipeline
    {
        VkComputePipelineCreateInfo pipeline_ci = {0};
        pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_ci.stage = fft_pass_stage_ci;
        pipeline_ci.layout = vulkan_state.fft_pass_pipeline_layout;

        vkCreateComputePipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &pipeline_ci, 0, &vulkan_state.fft_pass_pipeline);
    }

    // define FFT finalize compute pipeline
    {
        VkComputePipelineCreateInfo pipeline_ci = {0};
        pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_ci.stage = fft_finalize_stage_ci;
        pipeline_ci.layout = vulkan_state.fft_finalize_pipeline_layout;
        
        vkCreateComputePipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &pipeline_ci, 0, &vulkan_state.fft_finalize_pipeline);
    }

    for (int in_flight_index = 0; in_flight_index < 2; in_flight_index++)
    {
        createInstanceBuffer(&vulkan_state.cube_instance_buffers[in_flight_index], 
            sizeof(CubeInstanceData) * CUBE_INSTANCE_CAPACITY, 
            &vulkan_state.cube_instance_memories[in_flight_index], 
            &vulkan_state.cube_instance_mappeds[in_flight_index]);
        createInstanceBuffer(&vulkan_state.water_instance_buffers[in_flight_index], 
            sizeof(WaterInstanceData) * WATER_INSTANCE_CAPACITY, 
            &vulkan_state.water_instance_memories[in_flight_index], 
            &vulkan_state.water_instance_mappeds[in_flight_index]);
        createInstanceBuffer(&vulkan_state.laser_instance_buffers[in_flight_index],
            sizeof(LaserInstanceData) * LASER_INSTANCE_CAPACITY,
            &vulkan_state.laser_instance_memories[in_flight_index],
            &vulkan_state.laser_instance_mappeds[in_flight_index]);
    }

    loadAllEntities();

    // PAINT TEXTURE RESOURCES

    // linear sampler for smooth interpolation when sampling the paint texture
    {
        VkSamplerCreateInfo paint_sampler_ci = {0};
        paint_sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        paint_sampler_ci.magFilter = VK_FILTER_LINEAR;
        paint_sampler_ci.minFilter = VK_FILTER_LINEAR;
        paint_sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        paint_sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        paint_sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        paint_sampler_ci.anisotropyEnable = VK_FALSE;
        paint_sampler_ci.maxAnisotropy = 1.0f;
        paint_sampler_ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        paint_sampler_ci.unnormalizedCoordinates = VK_FALSE;
        paint_sampler_ci.compareEnable = VK_FALSE;
        paint_sampler_ci.compareOp = VK_COMPARE_OP_ALWAYS;
        paint_sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        vkCreateSampler(vulkan_state.logical_device_handle, &paint_sampler_ci, 0, &vulkan_state.paint_sampler);
    }

    // paint image
    {
        VkImageCreateInfo paint_image_ci = {0};
        paint_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        paint_image_ci.imageType = VK_IMAGE_TYPE_2D;
        paint_image_ci.extent.width = WATER_PAINT_SIDE;
        paint_image_ci.extent.height = WATER_PAINT_SIDE;
        paint_image_ci.extent.depth = 1;
        paint_image_ci.mipLevels = 1;
        paint_image_ci.arrayLayers = 1;
        paint_image_ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        paint_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        paint_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        paint_image_ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        paint_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        paint_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &paint_image_ci, 0, &vulkan_state.paint_image);

        VkMemoryRequirements paint_memory_requirements = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.paint_image, &paint_memory_requirements);

        VkMemoryAllocateInfo paint_alloc = {0};
        paint_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        paint_alloc.allocationSize = paint_memory_requirements.size;
        paint_alloc.memoryTypeIndex = findMemoryType(paint_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &paint_alloc, 0, &vulkan_state.paint_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.paint_image, vulkan_state.paint_image_memory, 0);

        VkImageViewCreateInfo paint_view_ci = {0};
        paint_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        paint_view_ci.image = vulkan_state.paint_image;
        paint_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        paint_view_ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        paint_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        paint_view_ci.subresourceRange.baseMipLevel = 0;
        paint_view_ci.subresourceRange.levelCount = 1;
        paint_view_ci.subresourceRange.baseArrayLayer = 0;
        paint_view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &paint_view_ci, 0, &vulkan_state.paint_image_view);
    }

    // persistent staging buffer
    {
        VkDeviceSize paint_size_bytes = (VkDeviceSize)WATER_PAINT_SIDE * WATER_PAINT_SIDE * sizeof(Vec4);

        VkBufferCreateInfo staging_buffer_ci = {0};
        staging_buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        staging_buffer_ci.size = paint_size_bytes;
        staging_buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        staging_buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(vulkan_state.logical_device_handle, &staging_buffer_ci, 0, &vulkan_state.paint_staging_buffer);

        VkMemoryRequirements staging_memory_requirements = {0};
        vkGetBufferMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.paint_staging_buffer, &staging_memory_requirements);

        VkMemoryAllocateInfo staging_alloc = {0};
        staging_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        staging_alloc.allocationSize = staging_memory_requirements.size;
        staging_alloc.memoryTypeIndex = findMemoryType(staging_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &staging_alloc, 0, &vulkan_state.paint_staging_memory);
        vkBindBufferMemory(vulkan_state.logical_device_handle, vulkan_state.paint_staging_buffer, vulkan_state.paint_staging_memory, 0);

        vkMapMemory(vulkan_state.logical_device_handle, vulkan_state.paint_staging_memory, 0, paint_size_bytes, 0, &vulkan_state.paint_staging_mapped);
        memset(vulkan_state.paint_staging_mapped, 0, (size_t)paint_size_bytes);
    }

    // water paint descriptor set
    {
        VkDescriptorSetAllocateInfo paint_desc_alloc = {0};
        paint_desc_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        paint_desc_alloc.descriptorPool = vulkan_state.descriptor_pool;
        paint_desc_alloc.descriptorSetCount = 1;
        paint_desc_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;

        vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &paint_desc_alloc, &vulkan_state.paint_descriptor_set);

        VkDescriptorImageInfo paint_desc_info = {0};
        paint_desc_info.sampler = vulkan_state.paint_sampler;
        paint_desc_info.imageView = vulkan_state.paint_image_view;
        paint_desc_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet paint_desc_write = {0};
        paint_desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        paint_desc_write.dstSet = vulkan_state.paint_descriptor_set;
        paint_desc_write.dstBinding = 0;
        paint_desc_write.descriptorCount = 1;
        paint_desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        paint_desc_write.pImageInfo = &paint_desc_info;

        vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &paint_desc_write, 0, 0);
    }

    // generate h0 spectrum at startup TODO: put this in function, call each time change world with new parameters. for now, all worlds have same parameters.
    {
        VkCommandBufferAllocateInfo cb_alloc = {0};
        cb_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cb_alloc.commandPool = vulkan_state.graphics_command_pool_handle;
        cb_alloc.commandBufferCount = 1;

        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(vulkan_state.logical_device_handle, &cb_alloc, &command_buffer);

        VkCommandBufferBeginInfo cb_begin = {0};
        cb_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cb_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command_buffer, &cb_begin);

        VkImage images_to_transition[4] =
        {
            vulkan_state.h0_image,
            vulkan_state.fft_buffer_a_image,
            vulkan_state.fft_buffer_b_image,
            vulkan_state.displacement_image,
        };
        for (int image_index = 0; image_index < 4; image_index++)
        {
            imageBarrier(command_buffer, images_to_transition[image_index],
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, VK_ACCESS_SHADER_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
        }

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_spectrum_pipeline);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_spectrum_pipeline_layout, 0, 1, &vulkan_state.h0_descriptor_set, 0, 0);

        FFTSpectrumPushConstants pc = {0};
        pc.texture_size = FFT_SIZE;
        pc.water_tile_length = water_tile_length;
        pc.wind_direction_x = -0.5f;
        pc.wind_direction_z = -1.0f;
        pc.peak_frequency = 2.0f;
        pc.peak_enhancement = 10.0f;
        pc.depth = 2.0f; // TODO: should i actually put this it 1m or whatever my actual depth is? maybe a paint input for depth, even if the depth is the same everywhere?
        pc.amplitude = water_amplitude;
        pc.gravity = 9.81f;
        pc.random_seed = 1337;

        vkCmdPushConstants(command_buffer, vulkan_state.fft_spectrum_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FFTSpectrumPushConstants), &pc);
        vkCmdDispatch(command_buffer, FFT_SIZE / 16, FFT_SIZE / 16, 1);

        vkEndCommandBuffer(command_buffer);

        VkSubmitInfo submit = {0};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer;

        vkQueueSubmit(vulkan_state.graphics_queue_handle, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkan_state.graphics_queue_handle);

        vkFreeCommandBuffers(vulkan_state.logical_device_handle, vulkan_state.graphics_command_pool_handle, 1, &command_buffer);
    }

    vulkan_state.paint_image_first_upload = true;
}

void vulkanSubmitFrame(DrawCommand* draw_commands, int32 draw_command_count, float water_time_from_game, float water_plane_from_game, Camera camera_from_game, ShaderMode shader_mode_from_game, WaterPaintTexture* paint_from_game)
{  
    vkWaitForFences(vulkan_state.logical_device_handle, 1, &vulkan_state.in_flight_fences[vulkan_state.current_frame], VK_TRUE, UINT64_MAX);

    vulkan_camera = camera_from_game;
    shader_mode = shader_mode_from_game;
    water_time = water_time_from_game;
    vulkan_state.water_paint_texture = paint_from_game;
    vulkan_state.water_plane_y = water_plane_from_game;

    sprite_instance_count = 0;
    cube_instance_count = 0;
    laser_instance_count = 0;
    model_instance_count = 0;
    model_editor_outline_instance_count = 0;
    water_instance_count = 0;

    for (int asset_index = 0; asset_index < draw_command_count; asset_index++)
    {
        DrawCommand* command = &draw_commands[asset_index];
        SpriteId sprite_id = command->sprite_id;
        AssetType type = command->type;

        if (type == CUBE_3D)
        {
            Vec4 uv_rect = spriteUV(sprite_id, type, ATLAS_3D_WIDTH, ATLAS_3D_HEIGHT);

            Cube* cube = &cube_instances[cube_instance_count++];
            cube->asset_index = (uint32)vulkan_state.atlas_3d_asset_index;
            cube->coords      = command->coords;
            cube->scale       = command->scale;
            cube->rotation    = command->rotation;
            cube->uv          = uv_rect;
        }
        else if (type == MODEL_3D)
        {
            Model* model = &model_instances[model_instance_count++];
            model->model_id = (uint32)sprite_id;
            model->coords   = command->coords;
            model->scale    = command->scale;
            model->rotation = command->rotation;
            model->color    = command->color;
        }
        else if (type == WATER_3D)
		{
            Water* water = &water_instances[water_instance_count++];
            water->coords = command->coords;
        }
        else if (type == LASER)
        {
            Laser* laser = &laser_instances[laser_instance_count++];
            laser->center   = command->coords;
            laser->length   = command->scale.z;
            laser->rotation = command->rotation;
            laser->color    = command->color;
            laser->start_clip_plane = command->start_clip_plane;
            laser->end_clip_plane = command->end_clip_plane;
        }
        else if (type == OUTLINE_3D)
        {
            Model* model = &model_editor_outline_instances[model_editor_outline_instance_count++];
            model->model_id = (uint32)sprite_id;
            model->coords   = command->coords;
            model->scale    = command->scale;
            model->rotation = command->rotation;
        }
        else if (type == SPRITE_2D)
        {
            int32 atlas_asset_index;
            int32 atlas_width;
            int32 atlas_height;

            if (spriteIsFont(sprite_id))
            {
                atlas_asset_index = vulkan_state.atlas_font_asset_index;
                atlas_width  = ATLAS_FONT_WIDTH;
                atlas_height = ATLAS_FONT_HEIGHT;
            }
            else
            {
                atlas_asset_index = vulkan_state.atlas_2d_asset_index;
                atlas_width  = ATLAS_2D_WIDTH;
                atlas_height = ATLAS_2D_HEIGHT;
            }

            Sprite* sprite = &sprite_instances[sprite_instance_count++];
            sprite->asset_index = (uint32)atlas_asset_index;
            sprite->coords      = command->coords;
            sprite->size        = command->scale;
            sprite->alpha       = command->color.w;
            sprite->uv          = spriteUV(sprite_id, type, atlas_width, atlas_height);
        }
    }

    // fill cube instance buffer
    CubeInstanceData* cube_gpu_instances = (CubeInstanceData*)vulkan_state.cube_instance_mappeds[vulkan_state.current_frame];
    for (uint32 instance_index = 0; instance_index < cube_instance_count; instance_index++)
    {
        Cube* cube = &cube_instances[instance_index];
        mat4BuildBasicTRS(cube_gpu_instances[instance_index].model, cube->coords); // assumption that all cubes aren't rotated and are at unit scale
        //mat4BuildTRS(cube_gpu_instances[instance_index].model, cube->coords, cube->rotation, cube->scale);
        cube_gpu_instances[instance_index].uv_rect = cube->uv;
    }

    // fill water instance buffer
    WaterInstanceData* water_gpu_instances = (WaterInstanceData*)vulkan_state.water_instance_mappeds[vulkan_state.current_frame];
    for (uint32 instance_index = 0; instance_index < water_instance_count; instance_index++)
    {
        Water* water = &water_instances[instance_index];
        mat4BuildBasicTRS(water_gpu_instances[instance_index].model, water->coords);
    }

    // fill laser instance buffer
    LaserInstanceData* laser_gpu_instances = (LaserInstanceData*)vulkan_state.laser_instance_mappeds[vulkan_state.current_frame];
    for (uint32 laser_index = 0; laser_index < laser_instance_count; laser_index++)
    {
        Laser* laser = &laser_instances[laser_index];
        LaserInstanceData* instance = &laser_gpu_instances[laser_index];

        instance->center = laser->center;
        instance->length = laser->length;
        instance->rotation = laser->rotation;
        instance->color = (Vec4){ laser->color.x, laser->color.y, laser->color.z, 0.1f };
        instance->start_clip_plane = laser->start_clip_plane;
        instance->end_clip_plane = laser->end_clip_plane;
    }
}

void vulkanDraw(bool do_profiling_output)
{
    uint32 swapchain_image_index = 0;
    VkResult acquire_result = vkAcquireNextImageKHR(vulkan_state.logical_device_handle, vulkan_state.swapchain_handle, UINT64_MAX, vulkan_state.image_available_semaphores[vulkan_state.current_frame], VK_NULL_HANDLE, &swapchain_image_index);

    // profiling
    uint32 pool_index = vulkan_state.timestamp_frame_index % 3;
    VkQueryPool current_pool = vulkan_state.timestamp_query_pools[pool_index];
    uint32 query_index = 0;

    switch (acquire_result)
    {
        case VK_SUCCESS: break;
        case VK_SUBOPTIMAL_KHR: break;
        case VK_ERROR_OUT_OF_DATE_KHR: return;
        case VK_ERROR_SURFACE_LOST_KHR: return;
        default: return;
    }

    if (vulkan_state.images_in_flight[swapchain_image_index] != VK_NULL_HANDLE)
    {
        vkWaitForFences(vulkan_state.logical_device_handle, 1, &vulkan_state.images_in_flight[swapchain_image_index], VK_TRUE, UINT64_MAX);
    }

    vulkan_state.images_in_flight[swapchain_image_index] = vulkan_state.in_flight_fences[vulkan_state.current_frame];
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    vkResetFences(vulkan_state.logical_device_handle, 1, &vulkan_state.in_flight_fences[vulkan_state.current_frame]);

    VkCommandBuffer command_buffer = vulkan_state.swapchain_command_buffers[swapchain_image_index];
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo command_buffer_begin_info = {0};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    // profiling
    vkCmdResetQueryPool(command_buffer, current_pool, 0, vulkan_state.timestamp_query_count);

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // FFT TIME EVOLUTION DISPATCH
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_evolved_pipeline);

        VkDescriptorSet evolved_sets[2] =
        {
            vulkan_state.h0_descriptor_set,
            vulkan_state.fft_buffer_a_descriptor_set,
        };
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_evolved_pipeline_layout, 0, 2, evolved_sets, 0, 0);

        FFTEvolvedPushConstants pc = {0};
        pc.texture_size = FFT_SIZE;
        pc.water_tile_length = water_tile_length;
        pc.gravity = 9.81f;
        pc.time = water_time;

        vkCmdPushConstants(command_buffer, vulkan_state.fft_evolved_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FFTEvolvedPushConstants), &pc);
        vkCmdDispatch(command_buffer, FFT_SIZE / 16, FFT_SIZE / 16, 1);

        memoryBarrier(command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // FFT PASSES (8 horizontal, then 8 vertical)
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_pass_pipeline);

        int32 log2_fft_size = 8; // log2(256)
        
        // tracks which buffer is currently the source
        bool source_is_a = true;

        for (int32 pass_index = 0; pass_index < 2 * log2_fft_size; pass_index++)
        {
            int32 direction = (pass_index < log2_fft_size) ? 0 : 1; // first 8 horizontal, next 8 vertical
            int32 level = (pass_index % log2_fft_size) + 1;

            VkDescriptorSet pass_sets[2] =
            {
                source_is_a ? vulkan_state.fft_buffer_a_descriptor_set : vulkan_state.fft_buffer_b_descriptor_set, // source
                source_is_a ? vulkan_state.fft_buffer_b_descriptor_set : vulkan_state.fft_buffer_a_descriptor_set, // dest
            };

            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_pass_pipeline_layout, 0, 2, pass_sets, 0, 0);

            FFTPassPushConstants pc = {0};
            pc.texture_size = FFT_SIZE;
            pc.level = level;
            pc.direction = direction;

            vkCmdPushConstants(command_buffer, vulkan_state.fft_pass_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FFTPassPushConstants), &pc);

            vkCmdDispatch(command_buffer, (FFT_SIZE / 2) / 16, FFT_SIZE / 16, 1);

            memoryBarrier(command_buffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            source_is_a = !source_is_a;
        }
    }

    // FFT FINALIZE: real part of fft_buffer_a, normalize
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_finalize_pipeline);
        
        VkDescriptorSet finalize_sets[2] =
        {
            vulkan_state.fft_buffer_a_descriptor_set,   // source (read)
            vulkan_state.displacement_descriptor_set,   // destination (write)
        };
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_state.fft_finalize_pipeline_layout, 0, 2, finalize_sets, 0, 0);
        
        FFTFinalizePushConstants pc = {0};
        pc.texture_size = FFT_SIZE;
        pc.water_tile_length = water_tile_length;
        
        vkCmdPushConstants(command_buffer, vulkan_state.fft_finalize_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FFTFinalizePushConstants), &pc);
        
        // dispatch one thread per texel
        vkCmdDispatch(command_buffer, FFT_SIZE / 16, FFT_SIZE / 16, 1);
        
        memoryBarrier(command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // upload paint texture if dirty
    if (vulkan_state.water_paint_texture && vulkan_state.water_paint_texture->dirty)
    {
        // TODO: slow... will want to either expose this mapped memory to game, and handle like that, or only write affected pixels on any given frame
        memcpy(vulkan_state.paint_staging_mapped, vulkan_state.water_paint_texture->values, sizeof(Vec4) * WATER_PAINT_SIDE * WATER_PAINT_SIDE);

        imageBarrier(command_buffer, vulkan_state.paint_image,
            vulkan_state.paint_image_first_upload ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            vulkan_state.paint_image_first_upload ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            vulkan_state.paint_image_first_upload ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        // copy staging to image
        VkBufferImageCopy paint_copy = {0};
        paint_copy.bufferOffset = 0;
        paint_copy.bufferRowLength = 0;
        paint_copy.bufferImageHeight = 0;
        paint_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        paint_copy.imageSubresource.mipLevel = 0;
        paint_copy.imageSubresource.baseArrayLayer = 0;
        paint_copy.imageSubresource.layerCount = 1;
        paint_copy.imageOffset = (VkOffset3D){ 0, 0, 0 };
        paint_copy.imageExtent = (VkExtent3D){ WATER_PAINT_SIDE, WATER_PAINT_SIDE, 1 };

        vkCmdCopyBufferToImage(command_buffer, vulkan_state.paint_staging_buffer, vulkan_state.paint_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &paint_copy);

        imageBarrier(command_buffer, vulkan_state.paint_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        vulkan_state.paint_image_first_upload = false;
        vulkan_state.water_paint_texture->dirty = false;
    }

    VkClearValue clear_values[3];
    clear_values[0].color.float32[0] = 0.005f;
    clear_values[0].color.float32[1] = 0.008f;
    clear_values[0].color.float32[2] = 0.02f;
    clear_values[0].color.float32[3] = 1.0f;
    clear_values[1].depthStencil.depth = 1.0f;
    clear_values[1].depthStencil.stencil = 0;
    clear_values[2].color.float32[0] = 0.0f;
    clear_values[2].color.float32[1] = 0.0f;
    clear_values[2].color.float32[2] = 0.0f;
    clear_values[2].color.float32[3] = 0.0f;

    // compute 16:9 letterbox viewport
    float target_aspect = 16.0f / 9.0f;
    float window_width = (float)vulkan_state.swapchain_extent.width;
    float window_height = (float)vulkan_state.swapchain_extent.height;
    float window_aspect = window_width / window_height;

    float viewport_width, viewport_height, viewport_x, viewport_y;
    if (window_aspect > target_aspect)
    {
        viewport_height = window_height;
        viewport_width = window_height * target_aspect;
        viewport_x = (window_width - viewport_width) * 0.5f;
        viewport_y = 0.0f;
    }
    else
    {
        viewport_width = window_width;
        viewport_height = window_width / target_aspect;
        viewport_x = 0.0f;
        viewport_y = (window_height - viewport_height) * 0.5f;
    }

    VkViewport viewport = {0};
    viewport.width = viewport_width;
    viewport.height = -viewport_height;
    viewport.x = viewport_x;
    viewport.y = viewport_y + viewport_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {0};
    scissor.offset.x = (int32)viewport_x;
    scissor.offset.y = (int32)viewport_y;
    scissor.extent.width = (uint32)viewport_width;
    scissor.extent.height = (uint32)viewport_height;

    float aspect = target_aspect;
    float projection_matrix[16];
    float view_matrix[16];
    mat4BuildPerspective(projection_matrix, vulkan_camera.fov * (6.283185f / 360.0f), aspect, 1.0f, 300.0f);
    mat4BuildViewFromQuat(view_matrix, vulkan_camera.coords, vulkan_camera.rotation);

    float reflected_view_matrix[16];
    mat4BuildReflectedView(reflected_view_matrix, vulkan_camera.coords, vulkan_camera.rotation, vulkan_state.water_plane_y);

    // fill view constants for this frame
    {
        char* view_constants_base = (char*)vulkan_state.scene_ubo_mappeds[vulkan_state.current_frame];

        // different view constants for each unique view
        ViewConstants* main_view_constants       = (ViewConstants*)(view_constants_base + VIEW_MAIN       * vulkan_state.view_constants_stride);
        ViewConstants* reflection_view_constants = (ViewConstants*)(view_constants_base + VIEW_REFLECTION * vulkan_state.view_constants_stride);
        ViewConstants* sprite_view_constants     = (ViewConstants*)(view_constants_base + VIEW_SPRITE     * vulkan_state.view_constants_stride);

        float identity_matrix[16];
        mat4Identity(identity_matrix);

        float focal_length = (float)vulkan_state.swapchain_extent.height / (TAU * tanf(vulkan_camera.fov / 360.0f));

        float orthographic_matrix[16];
        mat4BuildOrtho(orthographic_matrix, 0.0f, (float)vulkan_state.swapchain_extent.width, 0.0f, (float)vulkan_state.swapchain_extent.height, 0.0f, 1.0f);

        // main camera
        float main_view_projection[16];
        float main_inverse_view_projection[16];
        mat4Multiply(main_view_projection, projection_matrix, view_matrix);
        mat4Inverse(main_inverse_view_projection, main_view_projection);

        Vec3 sun_direction = { -0.5f, -1.0f, -0.15f };
        float covered_tiles_from_zero = 50.0f;
        Vec3 coverage_center = { covered_tiles_from_zero / 2.0f, 0.0f, covered_tiles_from_zero / 2.0f };
        float light_view_projection[16];
        mat4BuildDirectionalLight(light_view_projection, sun_direction, coverage_center, covered_tiles_from_zero / 2.0f);

        memcpy(main_view_constants->view,            view_matrix,                  sizeof(float) * 16);
        memcpy(main_view_constants->proj,            projection_matrix,            sizeof(float) * 16);
        memcpy(main_view_constants->view_proj,       main_view_projection,         sizeof(float) * 16);
        memcpy(main_view_constants->inv_view_proj,   main_inverse_view_projection, sizeof(float) * 16);
        memcpy(main_view_constants->light_view_proj, light_view_projection,        sizeof(float) * 16);

        main_view_constants->camera_position           = (Vec4){ vulkan_camera.coords.x, vulkan_camera.coords.y, vulkan_camera.coords.z, 0.0f };
        main_view_constants->light_direction           = (Vec4){ sun_direction.x, sun_direction.y, sun_direction.z, 0.0 };
        main_view_constants->water_plane_y             = vulkan_state.water_plane_y;
        main_view_constants->discard_below_water_plane = false;
        main_view_constants->time                      = water_time;
        main_view_constants->water_tile_length         = water_tile_length;
        main_view_constants->focal_length              = focal_length;

        // reflection camera: same globals, reflected view + real water plane
        float reflection_view_projection[16];
        float reflection_inverse_view_projection[16];
        mat4Multiply(reflection_view_projection, projection_matrix, reflected_view_matrix);
        mat4Inverse(reflection_inverse_view_projection, reflection_view_projection);

        *reflection_view_constants = *main_view_constants;
        memcpy(reflection_view_constants->view,          reflected_view_matrix,              sizeof(float) * 16);
        memcpy(reflection_view_constants->view_proj,     reflection_view_projection,         sizeof(float) * 16);
        memcpy(reflection_view_constants->inv_view_proj, reflection_inverse_view_projection, sizeof(float) * 16);
        reflection_view_constants->discard_below_water_plane = true;

        // sprite camera: identity view, ortho projection
        *sprite_view_constants = *main_view_constants;
        memcpy(sprite_view_constants->view,      identity_matrix,     sizeof(float) * 16);
        memcpy(sprite_view_constants->proj,      orthographic_matrix, sizeof(float) * 16);
        memcpy(sprite_view_constants->view_proj, orthographic_matrix, sizeof(float) * 16);
    }

    // RENDER PASSES

    VkRenderPassBeginInfo render_pass_begin_info = {0};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = vulkan_state.render_pass_handle;
    render_pass_begin_info.framebuffer = vulkan_state.swapchain_framebuffers[swapchain_image_index];
    render_pass_begin_info.renderArea.offset = (VkOffset2D){ 0, 0 };
    render_pass_begin_info.renderArea.extent = vulkan_state.swapchain_extent;
    render_pass_begin_info.clearValueCount = 3;
    render_pass_begin_info.pClearValues = clear_values;

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // SHADOW PASS
    {
        VkClearValue shadow_clear = {0};
        shadow_clear.depthStencil.depth = 1.0f;

        VkRenderPassBeginInfo shadow_rp_begin = {0};
        shadow_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadow_rp_begin.renderPass = vulkan_state.shadow_render_pass;
        shadow_rp_begin.framebuffer = vulkan_state.shadow_framebuffer;
        shadow_rp_begin.renderArea.offset = (VkOffset2D){ 0, 0 };
        shadow_rp_begin.renderArea.extent = (VkExtent2D){ SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION };
        shadow_rp_begin.clearValueCount = 1;
        shadow_rp_begin.pClearValues = &shadow_clear;

        vkCmdBeginRenderPass(command_buffer, &shadow_rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        // full shadow map
        VkViewport shadow_viewport = {0};
        shadow_viewport.x = 0.0f;
        shadow_viewport.y = 0.0f;
        shadow_viewport.width  = (float)SHADOW_MAP_RESOLUTION;
        shadow_viewport.height = (float)SHADOW_MAP_RESOLUTION;
        shadow_viewport.minDepth = 0.0f;
        shadow_viewport.maxDepth = 1.0f;

        VkRect2D shadow_scissor = {0};
        shadow_scissor.extent.width  = SHADOW_MAP_RESOLUTION;
        shadow_scissor.extent.height = SHADOW_MAP_RESOLUTION;

        vkCmdSetViewport(command_buffer, 0, 1, &shadow_viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &shadow_scissor);

        uint32 shadow_view_constants_offset = VIEW_MAIN * vulkan_state.view_constants_stride;

        // cube casters
        if (cube_instance_count > 0)
        {
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.shadow_cube_pipeline);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.shadow_pipeline_layout, 0, 1, &vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame], 1, &shadow_view_constants_offset);

            VkBuffer cube_buffers[2] = { vulkan_state.cube_vertex_buffer, vulkan_state.cube_instance_buffers[vulkan_state.current_frame] };
            VkDeviceSize cube_offsets[2] = { 0, 0 };
            vkCmdBindVertexBuffers(command_buffer, 0, 2, cube_buffers, cube_offsets);
            vkCmdBindIndexBuffer(command_buffer, vulkan_state.cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(command_buffer, vulkan_state.cube_index_count, cube_instance_count, 0, 0, 0);
        }

        // model casters
        if (model_instance_count > 0)
        {
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.shadow_model_pipeline);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.shadow_pipeline_layout, 0, 1, &vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame], 1, &shadow_view_constants_offset);

            for (uint32 model_instance_index = 0; model_instance_index < model_instance_count; model_instance_index++)
            {
                Model* model = &model_instances[model_instance_index];
                LoadedModel* model_data = &vulkan_state.loaded_models[model->model_id - MODEL_3D_VOID];
                if (model_data->index_count == 0) continue;

                float model_matrix[16];
                mat4BuildTRS(model_matrix, model->coords, model->rotation, model->scale);

                vkCmdPushConstants(command_buffer, vulkan_state.shadow_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, model_matrix);

                VkDeviceSize vertex_offset = 0;
                vkCmdBindVertexBuffers(command_buffer, 0, 1, &model_data->vertex_buffer, &vertex_offset);
                vkCmdBindIndexBuffer(command_buffer, model_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);

                vkCmdDrawIndexed(command_buffer, model_data->index_count, 1, 0, 0, 0);
            }
        }

        vkCmdEndRenderPass(command_buffer);
    }

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // REFLECTION PASS
    {
        uint32 reflection_width  = reflectionExtent(vulkan_state.swapchain_extent.width);
        uint32 reflection_height = reflectionExtent(vulkan_state.swapchain_extent.height);

        VkClearValue reflection_clears[2] = {0};
        reflection_clears[0].color.float32[0] = 0.1f; // much brighter clear values
        reflection_clears[0].color.float32[1] = 0.2f;
        reflection_clears[0].color.float32[2] = 0.4f;
        reflection_clears[0].color.float32[3] = 1.0f;
        reflection_clears[1].depthStencil.depth = 1.0f;
        reflection_clears[1].depthStencil.stencil = 0;

        VkRenderPassBeginInfo reflection_rp_begin = {0};
        reflection_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        reflection_rp_begin.renderPass = vulkan_state.reflection_render_pass;
        reflection_rp_begin.framebuffer = vulkan_state.reflection_framebuffer;
        reflection_rp_begin.renderArea.offset = (VkOffset2D){ 0, 0 };
        reflection_rp_begin.renderArea.extent = (VkExtent2D){ reflection_width, reflection_height };
        reflection_rp_begin.clearValueCount = 2;
        reflection_rp_begin.pClearValues = reflection_clears;

        vkCmdBeginRenderPass(command_buffer, &reflection_rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        float reflection_scale = 1.0f / (float)REFLECTION_DOWNSCALE;

        VkViewport reflection_viewport = viewport;
        reflection_viewport.y      *= reflection_scale;
        reflection_viewport.x      *= reflection_scale;
        reflection_viewport.width  *= reflection_scale;
        reflection_viewport.height *= reflection_scale;

        VkRect2D reflection_scissor = {0};
        reflection_scissor.offset.x      = scissor.offset.x / REFLECTION_DOWNSCALE;
        reflection_scissor.offset.y      = scissor.offset.y / REFLECTION_DOWNSCALE;
        reflection_scissor.extent.width  = scissor.extent.width  / REFLECTION_DOWNSCALE;
        reflection_scissor.extent.height = scissor.extent.height / REFLECTION_DOWNSCALE;

        vkCmdSetViewport(command_buffer, 0, 1, &reflection_viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &reflection_scissor);

        // cubes
        if (cube_instance_count > 0)
        {
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.cube_reflection_pipeline);

            VkBuffer cube_buffers[2] = { vulkan_state.cube_vertex_buffer, vulkan_state.cube_instance_buffers[vulkan_state.current_frame] };
            VkDeviceSize cube_offsets[2] = { 0, 0 };
            vkCmdBindVertexBuffers(command_buffer, 0, 2, cube_buffers, cube_offsets);
            vkCmdBindIndexBuffer(command_buffer, vulkan_state.cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);

            VkDescriptorSet cube_descriptor_sets[4] =
            {
                vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame],
                vulkan_state.descriptor_sets[vulkan_state.atlas_3d_asset_index],
                vulkan_state.displacement_sampled_descriptor_set,
                vulkan_state.shadow_map_descriptor_set,
            };

            uint32 cube_view_constants_offset = VIEW_REFLECTION * vulkan_state.view_constants_stride;
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.cube_pipeline_layout, 0, 4, cube_descriptor_sets, 1, &cube_view_constants_offset);

            vkCmdDrawIndexed(command_buffer, vulkan_state.cube_index_count, cube_instance_count, 0, 0, 0);
        }

        // models
        if (model_instance_count > 0)
        {
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.model_reflection_pipeline);

            VkDescriptorSet model_descriptor_sets[3] =
            {
                vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame],
                vulkan_state.displacement_sampled_descriptor_set,
                vulkan_state.shadow_map_descriptor_set,
            };
            uint32 model_view_constants_offset = VIEW_REFLECTION * vulkan_state.view_constants_stride;
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.model_pipeline_layout, 0, 3, model_descriptor_sets, 1, &model_view_constants_offset);

            for (uint32 model_instance_index = 0; model_instance_index < model_instance_count; model_instance_index++)
            {
                Model* model = &model_instances[model_instance_index];
                LoadedModel* model_data = &vulkan_state.loaded_models[model->model_id - MODEL_3D_VOID];
                if (model_data->index_count == 0) continue;

                VkDeviceSize vertex_offset = 0;
                float model_matrix[16];
                mat4BuildTRS(model_matrix, model->coords, model->rotation, model->scale);

                ModelPushConstants model_push_constants = {0};
                memcpy(model_push_constants.model, model_matrix, sizeof(model_push_constants.model));
                model_push_constants.color = model->color;

                vkCmdBindVertexBuffers(command_buffer, 0, 1, &model_data->vertex_buffer, &vertex_offset);
                vkCmdBindIndexBuffer(command_buffer, model_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(command_buffer, vulkan_state.model_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPushConstants), &model_push_constants);
                vkCmdDrawIndexed(command_buffer, model_data->index_count, 1, 0, 0, 0);
            }
        }

        vkCmdEndRenderPass(command_buffer);
    }

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // FULL SCENE PASS

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    // cubes
    if (cube_instance_count > 0)
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.cube_pipeline);

        VkBuffer cube_buffers[2] = { vulkan_state.cube_vertex_buffer, vulkan_state.cube_instance_buffers[vulkan_state.current_frame] };
        VkDeviceSize cube_offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(command_buffer, 0, 2, cube_buffers, cube_offsets);
        vkCmdBindIndexBuffer(command_buffer, vulkan_state.cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);

        VkDescriptorSet cube_descriptor_sets[4] =
        {
            vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame],
            vulkan_state.descriptor_sets[vulkan_state.atlas_3d_asset_index],
            vulkan_state.displacement_sampled_descriptor_set,
            vulkan_state.shadow_map_descriptor_set,
        };
        uint32 cube_view_constants_offset = VIEW_MAIN * vulkan_state.view_constants_stride;
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.cube_pipeline_layout, 0, 4, cube_descriptor_sets, 1, &cube_view_constants_offset);

        vkCmdDrawIndexed(command_buffer, vulkan_state.cube_index_count, cube_instance_count, 0, 0, 0);
    }

    // models
    if (model_instance_count > 0)
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.model_pipeline);

        VkDescriptorSet model_descriptor_sets[3] =
        {
            vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame],
            vulkan_state.displacement_sampled_descriptor_set,
            vulkan_state.shadow_map_descriptor_set,
        };
        uint32 model_view_constants_offset = VIEW_MAIN * vulkan_state.view_constants_stride;
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.model_pipeline_layout, 0, 3, model_descriptor_sets, 1, &model_view_constants_offset);

        for (uint32 model_instance_index = 0; model_instance_index < model_instance_count; model_instance_index++)
        {
            Model* model = &model_instances[model_instance_index];
            LoadedModel* model_data = &vulkan_state.loaded_models[model->model_id - MODEL_3D_VOID];
            if (model_data->index_count == 0) continue;

            VkDeviceSize vertex_offset = 0;
            float model_matrix[16];
            mat4BuildTRS(model_matrix, model->coords, model->rotation, model->scale);

            ModelPushConstants model_push_constants = {0};
            memcpy(model_push_constants.model, model_matrix, sizeof(model_push_constants.model));
            model_push_constants.color = model->color;

            vkCmdBindVertexBuffers(command_buffer, 0, 1, &model_data->vertex_buffer, &vertex_offset);
            vkCmdBindIndexBuffer(command_buffer, model_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(command_buffer, vulkan_state.model_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPushConstants), &model_push_constants);
            vkCmdDrawIndexed(command_buffer, model_data->index_count, 1, 0, 0, 0);
        }
    }

    vkCmdEndRenderPass(command_buffer);

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // OUTLINE PASS

    imageBarrier(command_buffer, vulkan_state.depth_image,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, // TODO: is this still right? not stenciling for this anymore
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderPassBeginInfo post_rp_begin = {0};
    post_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    post_rp_begin.renderPass = vulkan_state.outline_post_render_pass;
    post_rp_begin.framebuffer = vulkan_state.outline_post_framebuffers[swapchain_image_index];
    post_rp_begin.renderArea.offset = (VkOffset2D){0, 0};
    post_rp_begin.renderArea.extent = vulkan_state.swapchain_extent;
    post_rp_begin.clearValueCount = 0;

    vkCmdBeginRenderPass(command_buffer, &post_rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // TODO: clean this up, together with waterline pass
    VkViewport post_viewport = {0};
    post_viewport.x = 0;
    post_viewport.y = 0;
    post_viewport.width = (float)vulkan_state.swapchain_extent.width;
    post_viewport.height = (float)vulkan_state.swapchain_extent.height;
    post_viewport.minDepth = 0.0f;
    post_viewport.maxDepth = 1.0f;

    VkRect2D post_scissor = {0};
    post_scissor.extent.width = vulkan_state.swapchain_extent.width;
    post_scissor.extent.height = vulkan_state.swapchain_extent.height;

    vkCmdSetViewport(command_buffer, 0, 1, &post_viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &post_scissor);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.outline_post_pipeline);
    VkDescriptorSet post_sets[2] = { vulkan_state.depth_descriptor_set, vulkan_state.normal_descriptor_set };
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.outline_post_pipeline_layout, 0, 2, post_sets, 0, 0);

    float focal_length = (float)vulkan_state.swapchain_extent.height / (TAU * tanf(vulkan_camera.fov / 360.0f));

    float post_pc[6] = 
    {
        1.0f / (float)vulkan_state.swapchain_extent.width,
        1.0f / (float)vulkan_state.swapchain_extent.height,
        depth_threshold, 
        normal_threshold, 
        focal_length,
        (shader_mode == SHADER_MODE_OUTLINE_TEST) ? 1.0f : 0.0f
    };
    vkCmdPushConstants(command_buffer, vulkan_state.outline_post_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 6, post_pc);

    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // COPY UNDERWATER SCENE TO scene_copy_image

    imageBarrier(command_buffer, vulkan_state.swapchain_images[swapchain_image_index],
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    imageBarrier(command_buffer, vulkan_state.scene_copy_image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCopy copy_region = {0};
    copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.srcSubresource.layerCount = 1;
    copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.dstSubresource.layerCount = 1;
    copy_region.extent.width = vulkan_state.swapchain_extent.width;
    copy_region.extent.height = vulkan_state.swapchain_extent.height;
    copy_region.extent.depth = 1;

    vkCmdCopyImage(command_buffer,
        vulkan_state.swapchain_images[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vulkan_state.scene_copy_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copy_region);

    imageBarrier(command_buffer, vulkan_state.swapchain_images[swapchain_image_index],
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    imageBarrier(command_buffer, vulkan_state.scene_copy_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // WATER PASS

    if (water_instance_count > 0)
    {
        LoadedModel* water_data = &vulkan_state.loaded_models[MODEL_3D_WATER - MODEL_3D_VOID];
        if (water_data->index_count > 0)
        {
            VkClearValue water_clears[2] = {0};
            // [0] is color, ignored, but exists for indexing
            water_clears[1].color.float32[0] = 1.0f;

            VkRenderPassBeginInfo water_rp_begin = {0};
            water_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            water_rp_begin.renderPass = vulkan_state.water_render_pass;
            water_rp_begin.framebuffer = vulkan_state.water_framebuffers[swapchain_image_index];
            water_rp_begin.renderArea.offset = (VkOffset2D){0, 0};
            water_rp_begin.renderArea.extent = vulkan_state.swapchain_extent;
            water_rp_begin.clearValueCount = 2;
            water_rp_begin.pClearValues = water_clears;

            vkCmdBeginRenderPass(command_buffer, &water_rp_begin, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdSetViewport(command_buffer, 0, 1, &viewport);
            vkCmdSetScissor(command_buffer, 0, 1, &scissor);

            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.water_pipeline);

            VkDescriptorSet water_descriptor_sets[8] =
            {
                vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame],
                vulkan_state.scene_copy_descriptor_set,
                vulkan_state.depth_descriptor_set,
                vulkan_state.paint_descriptor_set,
                vulkan_state.displacement_sampled_descriptor_set,
                vulkan_state.reflection_descriptor_set,
                vulkan_state.descriptor_sets[vulkan_state.water_grid_asset_index],
                vulkan_state.descriptor_sets[vulkan_state.water_grid_normal_asset_index],
            };
            uint32 water_view_constants_offset = VIEW_MAIN * vulkan_state.view_constants_stride;
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.water_pipeline_layout, 0, 8, water_descriptor_sets, 1, &water_view_constants_offset);

            VkBuffer water_buffers[2] = { water_data->vertex_buffer, vulkan_state.water_instance_buffers[vulkan_state.current_frame] };
            VkDeviceSize water_offsets[2] = { 0, 0 };
            vkCmdBindVertexBuffers(command_buffer, 0, 2, water_buffers, water_offsets);
            vkCmdBindIndexBuffer(command_buffer, water_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(command_buffer, water_data->index_count, water_instance_count, 0, 0, 0);

            vkCmdEndRenderPass(command_buffer);
        }
    }

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // WATERLINE PASS

    if (water_instance_count > 0)
    {
        LoadedModel* water_data = &vulkan_state.loaded_models[MODEL_3D_WATER - MODEL_3D_VOID];
        if (water_data->index_count > 0)
        {
            VkRenderPassBeginInfo waterline_pass_begin = {0};
            waterline_pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            waterline_pass_begin.renderPass = vulkan_state.waterline_render_pass;
            waterline_pass_begin.framebuffer = vulkan_state.waterline_framebuffers[swapchain_image_index];
            waterline_pass_begin.renderArea.offset.x = 0;
            waterline_pass_begin.renderArea.offset.y = 0;
            waterline_pass_begin.renderArea.extent = vulkan_state.swapchain_extent;
            waterline_pass_begin.clearValueCount = 0;

            vkCmdBeginRenderPass(command_buffer, &waterline_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

            // TODO: clean this up, together with outline pass
            VkViewport waterline_viewport = {0};
            waterline_viewport.x = 0;
            waterline_viewport.y = 0;
            waterline_viewport.width = (float)vulkan_state.swapchain_extent.width;
            waterline_viewport.height = (float)vulkan_state.swapchain_extent.height;
            waterline_viewport.minDepth = 0.0f;
            waterline_viewport.maxDepth = 1.0f;

            VkRect2D waterline_scissor = {0};
            waterline_scissor.extent.width = vulkan_state.swapchain_extent.width;
            waterline_scissor.extent.height = vulkan_state.swapchain_extent.height;

            vkCmdSetViewport(command_buffer, 0, 1, &waterline_viewport);
            vkCmdSetScissor(command_buffer, 0, 1, &waterline_scissor);

            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.waterline_pipeline);

            VkDescriptorSet waterline_sets[2] =
            {
                vulkan_state.depth_descriptor_set,
                vulkan_state.water_depth_descriptor_set,
            };
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.waterline_pipeline_layout, 0, 2, waterline_sets, 0, 0);

            WaterlinePushConstants waterline_pc = {0};
            waterline_pc.texel_width = 1.0f / (float)vulkan_state.swapchain_extent.width;
            waterline_pc.texel_height = 1.0f / (float)vulkan_state.swapchain_extent.height;
            waterline_pc.max_depth_difference = 0.1f;
            waterline_pc.outline_radius_px = 1.0f;

            vkCmdPushConstants(command_buffer, vulkan_state.waterline_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(waterline_pc), &waterline_pc);

            vkCmdDraw(command_buffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(command_buffer);
        }
    }

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, current_pool, query_index++);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // OVERLAY PASS (editor outlines + lasers + sprites)

    // clear oit resources 
    {
        imageBarrier(command_buffer, vulkan_state.oit_head_image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkClearColorValue clear_val = {0};
        clear_val.uint32[0] = 0xFFFFFFFF;
        VkImageSubresourceRange clear_range = {0};
        clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clear_range.baseMipLevel = 0;
        clear_range.levelCount = 1;
        clear_range.baseArrayLayer = 0;
        clear_range.layerCount = 1;
        vkCmdClearColorImage(command_buffer, vulkan_state.oit_head_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_val, 1, &clear_range);

        imageBarrier(command_buffer, vulkan_state.oit_head_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        // clear counter to 0
        vkCmdFillBuffer(command_buffer, vulkan_state.oit_counter_buffer, 0, 4, 0);

        memoryBarrier(command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }

    VkRenderPassBeginInfo overlay_rp_begin = {0};
    overlay_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    overlay_rp_begin.renderPass = vulkan_state.overlay_render_pass;
    overlay_rp_begin.framebuffer = vulkan_state.overlay_framebuffers[swapchain_image_index];
    overlay_rp_begin.renderArea.offset = (VkOffset2D){0, 0};
    overlay_rp_begin.renderArea.extent = vulkan_state.swapchain_extent;
    overlay_rp_begin.clearValueCount = 0;

    vkCmdBeginRenderPass(command_buffer, &overlay_rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    // selected outlines (on top of everything)
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.editor_outline_pipeline);

    uint32 outline_view_constants_offset = VIEW_MAIN * vulkan_state.view_constants_stride;
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.editor_outline_pipeline_layout, 0, 1, &vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame], 1, &outline_view_constants_offset);

    // model outlines
    for (uint32 model_outline_index = 0; model_outline_index < model_editor_outline_instance_count; model_outline_index++)
    {
        Model* model = &model_editor_outline_instances[model_outline_index];

        LoadedModel* model_data = model->model_id == SPRITEID_ASSET_COUNT ? &vulkan_state.dummy_cube_model : &vulkan_state.loaded_models[model->model_id - MODEL_3D_VOID];
        if (model_data->index_count == 0) continue;

        VkDeviceSize vertex_offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &model_data->vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(command_buffer, model_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);

        float model_matrix[16];
        mat4BuildTRS(model_matrix, model->coords, model->rotation, model->scale);

        OutlinePushConstants outline_push_constants = {0};
        memcpy(outline_push_constants.model, model_matrix, sizeof(outline_push_constants.model));

        vkCmdPushConstants(command_buffer, vulkan_state.editor_outline_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutlinePushConstants), &outline_push_constants);
        vkCmdDrawIndexed(command_buffer, model_data->index_count, 1, 0, 0, 0);
    }

    // LASER PASS
    LoadedModel* laser_mesh = &vulkan_state.laser_cylinder_model;
    if (laser_mesh->index_count > 0 && laser_instance_count > 0 && shader_mode != SHADER_MODE_OUTLINE_TEST)
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.laser_pipeline);

        VkDescriptorSet laser_descriptor_sets[4] =
        {
            vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame],
            vulkan_state.oit_head_storage_descriptor_set,
            vulkan_state.oit_fragment_pool_descriptor_set,
            vulkan_state.oit_counter_descriptor_set,
        };
        uint32 laser_view_constants_offset = VIEW_MAIN * vulkan_state.view_constants_stride;
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.laser_pipeline_layout, 0, 4, laser_descriptor_sets, 1, &laser_view_constants_offset);

        VkBuffer laser_vertex_buffers[2] = { laser_mesh->vertex_buffer, vulkan_state.laser_instance_buffers[vulkan_state.current_frame] };
        VkDeviceSize laser_vertex_offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(command_buffer, 0, 2, laser_vertex_buffers, laser_vertex_offsets);
        vkCmdBindIndexBuffer(command_buffer, laser_mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(command_buffer, laser_mesh->index_count, laser_instance_count, 0, 0, 0);
    }

    memoryBarrier(command_buffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    // OIT resolve pass
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.oit_resolve_pipeline);

        VkDescriptorSet oit_sets[4] = 
        {
            vulkan_state.oit_head_storage_descriptor_set,
            vulkan_state.oit_fragment_pool_descriptor_set,
            vulkan_state.oit_counter_descriptor_set,
            vulkan_state.depth_descriptor_set,
        };
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.oit_resolve_pipeline_layout, 0, 4, oit_sets, 0, 0);

        float oit_depth_threshold = 0.5f;
        vkCmdPushConstants(command_buffer, vulkan_state.oit_resolve_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &oit_depth_threshold);

        vkCmdDraw(command_buffer, 3, 1, 0, 0);
    }

    // sprites
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline);

    VkDeviceSize sprite_vertex_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vulkan_state.sprite_vertex_buffer, &sprite_vertex_offset);
    vkCmdBindIndexBuffer(command_buffer, vulkan_state.sprite_index_buffer, 0, VK_INDEX_TYPE_UINT32);

    uint32 sprite_view_constants_offset = VIEW_SPRITE * vulkan_state.view_constants_stride;
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline_layout, 0, 1, &vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame], 1, &sprite_view_constants_offset);

    int32 last_sprite_asset = -1;

    for (uint32 sprite_instance_index = 0; sprite_instance_index < sprite_instance_count; sprite_instance_index++)
    {
        Sprite* sprite = &sprite_instances[sprite_instance_index];

        if ((int32)sprite->asset_index != last_sprite_asset)
        {
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline_layout, 1, 1, &vulkan_state.descriptor_sets[sprite->asset_index], 0, 0);
            last_sprite_asset = (int32)sprite->asset_index;
        }

        float model_matrix[16];
        Vec4 identity_quaternion = { 0.0f, 0.0f, 0.0f, 1.0f };
        mat4BuildTRS(model_matrix, sprite->coords, identity_quaternion, sprite->size);

        SpritePushConstants sprite_push_constants = {0};
        memcpy(sprite_push_constants.model, model_matrix, sizeof(sprite_push_constants.model));
        sprite_push_constants.uv_rect = sprite->uv;
        sprite_push_constants.color = (Vec4){ 0.0f, 0.0f, 0.0f, sprite->alpha };

        vkCmdPushConstants(command_buffer, vulkan_state.sprite_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SpritePushConstants), &sprite_push_constants);

        vkCmdDrawIndexed(command_buffer, vulkan_state.sprite_index_count, 1, 0, 0, 0);
    }

    // debug quad
    /*
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline);

        VkDeviceSize debug_vertex_offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &vulkan_state.sprite_vertex_buffer, &debug_vertex_offset);
        vkCmdBindIndexBuffer(command_buffer, vulkan_state.sprite_index_buffer, 0, VK_INDEX_TYPE_UINT32);

        uint32 debug_view_constants_offset = VIEW_SPRITE * vulkan_state.view_constants_stride;
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline_layout, 0, 1, &vulkan_state.view_constants_descriptor_sets[vulkan_state.current_frame], 1, &debug_view_constants_offset);

        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline_layout, 1, 1, &vulkan_state.shadow_map_descriptor_set, 0, 0);

        float debug_size = 300.0f;
        float debug_margin = 10.0f;
        Vec3 debug_coords =
        {
            (float)vulkan_state.swapchain_extent.width  - debug_margin - debug_size * 0.5f,
            (float)vulkan_state.swapchain_extent.height - debug_margin - debug_size * 0.5f,
            0.0f,
        };
        Vec3 debug_scale = { debug_size, debug_size, 1.0f };

        float debug_model[16];
        Vec4 identity_quaternion = { 0.0f, 0.0f, 0.0f, 1.0f };
        mat4BuildTRS(debug_model, debug_coords, identity_quaternion, debug_scale);

        SpritePushConstants debug_pc = {0};
        memcpy(debug_pc.model, debug_model, sizeof(debug_pc.model));
        debug_pc.uv_rect = (Vec4){ 0.0f, 0.0f, 1.0f, 1.0f };
        debug_pc.color   = (Vec4){ 0.0f, 0.0f, 0.0f, 1.0f }; // opaque

        vkCmdPushConstants(command_buffer, vulkan_state.sprite_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SpritePushConstants), &debug_pc);
        vkCmdDrawIndexed(command_buffer, vulkan_state.sprite_index_count, 1, 0, 0, 0);
    }
    */

    vkCmdEndRenderPass(command_buffer);

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, current_pool, query_index++);

    // profiling end
    vulkan_state.timestamp_query_counts[pool_index] = query_index;
    vulkan_state.timestamp_pool_valid[pool_index] = true;

    vkEndCommandBuffer(command_buffer);

    // SUBMIT
    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &vulkan_state.image_available_semaphores[vulkan_state.current_frame];
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vulkan_state.swapchain_command_buffers[swapchain_image_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &vulkan_state.render_finished_semaphores[vulkan_state.current_frame];

    VkResult submit_result = vkQueueSubmit(vulkan_state.graphics_queue_handle, 1, &submit_info, vulkan_state.in_flight_fences[vulkan_state.current_frame]);

    if (submit_result != VK_SUCCESS)
    {
        VkSubmitInfo empty_submit = {0};
        empty_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(vulkan_state.graphics_queue_handle, 1, &empty_submit, vulkan_state.in_flight_fences[vulkan_state.current_frame]);
    }

    // PRESENT
    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &vulkan_state.render_finished_semaphores[vulkan_state.current_frame];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &vulkan_state.swapchain_handle;
    present_info.pImageIndices = &swapchain_image_index;

    VkResult present_result = vkQueuePresentKHR(vulkan_state.present_queue_handle, &present_info);

    switch (present_result)
    {
        case VK_SUCCESS: break;
        case VK_SUBOPTIMAL_KHR: break;
        case VK_ERROR_OUT_OF_DATE_KHR:
        {
            vkDeviceWaitIdle(vulkan_state.logical_device_handle);
            createSwapchainResources();
            return;
        }
        case VK_ERROR_SURFACE_LOST_KHR: return;
        default: return;
    }

    vulkan_state.current_frame = (vulkan_state.current_frame + 1) % vulkan_state.frames_in_flight;

    // TEMP: profiling
    uint32 read_pool_index = (vulkan_state.timestamp_frame_index + 1) % 3;
    if (vulkan_state.timestamp_pool_valid[read_pool_index])
    {
        if (do_profiling_output)
        {
            uint32 count = vulkan_state.timestamp_query_counts[read_pool_index];
            vkGetQueryPoolResults(vulkan_state.logical_device_handle, vulkan_state.timestamp_query_pools[read_pool_index], 0, count, sizeof(uint64) * count, vulkan_state.timestamp_results[read_pool_index], sizeof(uint64), VK_QUERY_RESULT_64_BIT);
            uint64* t = vulkan_state.timestamp_results[read_pool_index];
            char* region_names[] = 
            {
                "fft", "setup", "shadow", "reflection", "scene", "outline", "scene copy", "water", "waterline", "overlay"
            };
            for (uint32 i = 0; i + 1 < count; i += 2)
            {
                double ns = (double)(t[i+1] - t[i]) * vulkan_state.timestamp_period;
                double ms = ns / 1e6;
                char line[128];
                snprintf(line, sizeof(line), "%s: %.3f ms\n", region_names[i/2], ms);
                OutputDebugStringA(line);
            }
        }
    }

    vulkan_state.timestamp_frame_index++;
}

void vulkanResize(uint32 width, uint32 height)
{
    if (width == 0 || height == 0) return;
    if (vulkan_state.logical_device_handle == VK_NULL_HANDLE) return;
    if (vulkan_state.swapchain_handle == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(vulkan_state.logical_device_handle);

    // destroy old resources
    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.depth_sampled_view, 0);

    for (uint32 image_index = 0; image_index < vulkan_state.swapchain_image_count; image_index++)
    {
        vkDestroyFramebuffer(vulkan_state.logical_device_handle, vulkan_state.swapchain_framebuffers[image_index], 0);
        vkDestroyFramebuffer(vulkan_state.logical_device_handle, vulkan_state.outline_post_framebuffers[image_index], 0);
        vkDestroyFramebuffer(vulkan_state.logical_device_handle, vulkan_state.water_framebuffers[image_index], 0);
        vkDestroyFramebuffer(vulkan_state.logical_device_handle, vulkan_state.overlay_framebuffers[image_index], 0);
        vkDestroyFramebuffer(vulkan_state.logical_device_handle, vulkan_state.waterline_framebuffers[image_index], 0);
        vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.swapchain_image_views[image_index], 0);
    }

    vkFreeCommandBuffers(vulkan_state.logical_device_handle, vulkan_state.graphics_command_pool_handle, vulkan_state.swapchain_image_count, vulkan_state.swapchain_command_buffers);

    // destroy depth view + image
    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.depth_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.depth_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.depth_image_memory, 0);

    // destroy normal view + image
    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.normal_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.normal_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.normal_image_memory, 0);

    // destroy water composite
    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.scene_copy_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.scene_copy_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.scene_copy_image_memory, 0);

    // destory water depth
    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.water_depth_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.water_depth_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.water_depth_image_memory, 0);
	
    // destroy OIT resources
    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.oit_head_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.oit_head_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.oit_head_memory, 0);

    vkDestroyBuffer(vulkan_state.logical_device_handle, vulkan_state.oit_fragment_pool, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.oit_fragment_pool_memory, 0);

    vkDestroyBuffer(vulkan_state.logical_device_handle, vulkan_state.oit_counter_buffer, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.oit_counter_memory, 0);

    // destroy reflection resources
    vkDestroyFramebuffer(vulkan_state.logical_device_handle, vulkan_state.reflection_framebuffer, 0);

    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.reflection_color_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.reflection_color_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.reflection_color_image_memory, 0);

    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_color_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_color_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_color_image_memory, 0);

    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_depth_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_depth_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.reflection_msaa_depth_image_memory, 0);

    // old swapchain is destroyed inside createSwapchainResources
    createSwapchainResources();
}

void vulkanShutdown(void)
{

}
