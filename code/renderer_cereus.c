#define VK_USE_PLATFORM_WIN32_KHR
#define STB_IMAGE_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION

#include <stdio.h>
#include "win32_vulkan_bridge.h"
#include <vulkan/vulkan.h>
#include "stb_image.h"
#include "cgltf.h"

#define LOG(text, ...) do { \
    char log_buffer[512]; \
    snprintf(log_buffer, sizeof(log_buffer), text, ##__VA_ARGS__); \
    OutputDebugStringA(log_buffer); \
} while(0)

DisplayInfo vulkan_display = {0};

const uint32 CUBE_INSTANCE_CAPACITY = 8192;
const uint32 WATER_INSTANCE_CAPACITY = 8192;

// TODO: set these in loadAsset where stb_image gives me width / height. store in CachedAsset.
const int32 ATLAS_2D_WIDTH = 128;
const int32 ATLAS_2D_HEIGHT = 128;
const int32 ATLAS_FONT_WIDTH = 120;
const int32 ATLAS_FONT_HEIGHT = 180;
const int32 ATLAS_3D_WIDTH = 480;
const int32 ATLAS_3D_HEIGHT = 320;

const char* ATLAS_2D_PATH 	= "data/sprites/atlas-2d.png";
const char* ATLAS_FONT_PATH = "data/sprites/atlas-font.png";
const char* ATLAS_3D_PATH 	= "data/sprites/atlas-3d.png";

bool first_submit_since_draw = true;

ShaderMode shader_mode = OLD;

float water_time = 0.0f;

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
}
Model;

typedef struct
{
    Vec3 coords;
}
Water;

// for instancing of cubes
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

typedef struct 
{
    Vec3 center;
    float length;
    Vec4 rotation;
    Vec3 color;
}
Laser;

typedef struct 
{
	VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    char path[256];
}
CachedAsset;

typedef struct
{
    float model[16];
    float view[16];
    float proj[16];
    Vec4 uv_rect;
    float alpha;
}
PushConstants; // TODO: rename

typedef struct 
{
    float model[16];
    float view[16];
    float proj[16];
    Vec4 color;
}
LaserPushConstants;

typedef struct 
{
    float view[16];
    float proj[16];
}
InstancedPushConstants;

typedef struct
{
    float view[16];
    float proj[16];
    float time;
}
WaterPushConstants;

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

typedef struct VulkanState
{
    // platform and instance
    RendererPlatformHandles platform_handles;
    VkInstance vulkan_instance_handle;
    VkSurfaceKHR surface_handle;
	VkPhysicalDevice physical_device_handle;

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

    // depth + normal (shared attachments)
    VkFormat depth_format;
    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
    VkImageView depth_sampled_view;
    VkDescriptorSet depth_descriptor_set;

    VkImage normal_image;
    VkDeviceMemory normal_image_memory;
    VkImageView normal_image_view;
    VkDescriptorSet normal_descriptor_set;

    // scene color copy for water shader
    VkImage scene_color_image;
    VkDeviceMemory scene_color_image_memory;
    VkImageView scene_color_image_view;
    VkDescriptorSet scene_color_descriptor_set;

    // commands + sync
    VkCommandPool graphics_command_pool_handle;
    VkCommandBuffer* swapchain_command_buffers;
	uint32 frames_in_flight;
	uint32 current_frame;
    VkSemaphore* image_available_semaphores; // semaphore(s) that handle WSI -> graphics. wsi produces swapchain image, graphics queue renders into that image.
    VkSemaphore* render_finished_semaphores; // semaphore(s) that handle graphics -> present. once graphics finishes rendering, graphics sends renders to be presented.
    VkFence* in_flight_fences; 
    VkFence* images_in_flight;

    // RENDER PASSES

    // scene pass (cubes, models, select outlines)
    VkRenderPass render_pass_handle;
    VkFramebuffer* swapchain_framebuffers;

    // outline post pass (black outlines on everything)
    VkRenderPass outline_post_render_pass;
    VkFramebuffer* outline_post_framebuffers;
 
    // render pass to sample depth for water buffer
    VkRenderPass water_render_pass;
    VkFramebuffer* water_framebuffers;

    // overlay pass (lasers, which color the outlines, and sprites, which go over the outlines)
    VkRenderPass overlay_render_pass;
    VkFramebuffer* overlay_framebuffers;

    // PIPELINES AND LAYOUTS

    // first render pass, for the main scene
    VkPipelineLayout default_graphics_pipeline_layout;

    VkPipeline cube_pipeline_handle;
    VkPipelineLayout cube_pipeline_layout; 

    VkPipeline outline_pipeline_handle;
	VkPipelineLayout outline_pipeline_layout;

    VkPipeline model_pipeline_handle;
    VkPipelineLayout model_pipeline_layout;

    VkPipeline water_pipeline_handle;
    VkPipelineLayout water_pipeline_layout;

    // second render pass (outlines, based on depth and normal)
    VkPipeline outline_post_pipeline;
    VkPipelineLayout outline_post_pipeline_layout;

    // third render pass (lasers + sprites)
    VkPipeline laser_fill_pipeline_handle;
    VkPipeline laser_outline_pipeline_handle;
    VkPipelineLayout laser_pipeline_layout;

    VkPipeline sprite_pipeline_handle;
    VkPipelineLayout sprite_pipeline_layout;

    // shared resources
    VkSampler pixel_art_sampler;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[1024];

    // asset cache
    CachedAsset asset_cache[256];
    uint32 asset_cache_count;
    int32 atlas_2d_asset_index;
	int32 atlas_font_asset_index;
    int32 atlas_3d_asset_index;

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
    VkBuffer cube_instance_buffer;
	VkDeviceMemory cube_instance_memory;
    void* cube_instance_mapped;
    uint32 cube_instance_capacity;

    VkBuffer water_instance_buffer;
    VkDeviceMemory water_instance_memory;
    void* water_instance_mapped;
    uint32 water_instance_capacity;

    // models
    LoadedModel loaded_models[64];
    LoadedModel laser_cylinder_model; // TODO: probably index everything into loaded models; figure out what order i want to put stuff in, if can't just take their id
}
VulkanState;

#define U0 (0.0f)
#define U1 (1.0f/3.0f)
#define U2 (2.0f/3.0f)
#define U3 (1.0f)

#define V0 (0.0f)
#define V1 (0.5f)
#define V2 (1.0f)

static const Vertex SPRITE_VERTICES[] =
{
    { -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,  0,0,1 },
    {  0.5f, -0.5f, 0.0f,  1.0f, 1.0f,  0,0,1 },
    {  0.5f,  0.5f, 0.0f,  1.0f, 0.0f,  0,0,1 },
    { -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,  0,0,1 },
};

static const Vertex CUBE_VERTICES[] = 
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

static const uint32 CUBE_INDICES[36] = 
{
    0, 1, 2,  0, 2, 3,
    4, 5, 6,  4, 6, 7,
    8, 9,10,  8,10,11,
   12,13,14, 12,14,15,
   16,17,18, 16,18,19,
   20,21,22, 20,22,23
};

static const uint32 SPRITE_INDICES[6] =
{
    0, 1, 2,
    0, 2, 3
};

VulkanState vulkan_state;

Vertex frame_vertex_stash[65536];
uint32 frame_vertex_count = 0;

Sprite sprite_instances[8192];
uint32 sprite_instance_count = 0;

Cube cube_instances[8192];
uint32 cube_instance_count = 0;

Cube outline_instances[1024];
uint32 outline_instance_count = 0;

Laser laser_instances[1024];
uint32 laser_instance_count = 0;

Model model_instances[1024];
uint32 model_instance_count = 0;

Model model_selected_outline_instances[1024];
uint32 model_selected_outline_instance_count = 0;

Water water_instances[8192];
uint32 water_instance_count = 0;

Camera vulkan_camera = {0};

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

bool readEntireFile(char* path, void** out_data, size_t* out_size)
{
	FILE* file = fopen(path, "rb"); // read binary
    if (!file) return false;
    fseek(file, 0, SEEK_END); // move stream's file position to 0 bytes from the end (i.e. just past the last byte)
   	long end_position = ftell(file); // return current file position, i.e. size of the file in bytes
    fseek(file, 0, SEEK_SET); // seeks back to the start, so the file can be read from byte 0
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
        if (spriteIsFont(id))
        {
            return (int32)id - (int32)SPRITE_2D_FONT_SPACE;
        }
        else
        {
            return (int32)id;
        }
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

int32 loadAsset(char* path)
{
    int width, height, channels;
    uint8* pixels = (uint8*)stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) return -1;

    VkDeviceSize image_size_bytes = width * height * 4; // 4 bytes per pixel
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
    image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
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
    VkImageMemoryBarrier to_transfer_barrier = {0}; // synchronization point - wait until image is written to transfer
    to_transfer_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; // arranged for fast writing
	to_transfer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_transfer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_transfer_barrier.image = texture_image;
	to_transfer_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_transfer_barrier.subresourceRange.baseMipLevel = 0;
	to_transfer_barrier.subresourceRange.levelCount = 1;
	to_transfer_barrier.subresourceRange.baseArrayLayer = 0;
	to_transfer_barrier.subresourceRange.layerCount = 1;
	to_transfer_barrier.srcAccessMask = 0; // no memory operations need to complete (we don't care about previous data here)
	to_transfer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkPipelineStageFlags to_transfer_source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags to_transfer_destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    
    vkCmdPipelineBarrier(command_buffer, to_transfer_source_stage, to_transfer_destination_stage, 0, 0, 0, 0, 0, 1, &to_transfer_barrier);

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

    // transition image to shader reading
	VkImageMemoryBarrier to_shader_barrier = {0};
    to_shader_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_shader_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // optimised for fragment shader reading (probably not entirely sequential)
    to_shader_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_barrier.image = texture_image;
	to_shader_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_shader_barrier.subresourceRange.baseMipLevel = 0;
	to_shader_barrier.subresourceRange.levelCount = 1;
	to_shader_barrier.subresourceRange.baseArrayLayer = 0;
    to_shader_barrier.subresourceRange.layerCount = 1;
    to_shader_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // wait for transfer writes to complete
    to_shader_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // make memory available for shader reads

    VkPipelineStageFlags to_shader_source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkPipelineStageFlags to_shader_destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    vkCmdPipelineBarrier(command_buffer, to_shader_source_stage, to_shader_destination_stage, 0, 0, 0, 0, 0, 1, &to_shader_barrier);

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
    image_view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
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

int32 getOrLoadAsset(char* path)
{
    // check if already loaded
    for (uint32 asset_cache_index = 0; asset_cache_index < vulkan_state.asset_cache_count; asset_cache_index++)
    {
        if (strcmp(vulkan_state.asset_cache[asset_cache_index].path, path) == 0) return (int32)asset_cache_index;
    }
    return loadAsset(path);
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
    cgltf_data* data = NULL;
    cgltf_result parse_result = cgltf_parse_file(&options, path, &data);
    if (parse_result != cgltf_result_success)
    {
        LOG("failed to parse gltf file: %s\n", path);
        return result;
    }

    cgltf_result load_result = cgltf_load_buffers(&options, data, path);
    if (load_result != cgltf_result_success)
    {
        LOG("failed to load gltf buffers: %s\n", path);
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
        LOG("no geometry in: %s\n", path);
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

    uploadBufferToLocalDevice(vertices, sizeof(Vertex) * total_verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &result.vertex_buffer, &result.vertex_memory);
    uploadBufferToLocalDevice(indices, sizeof(uint32) * total_indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &result.index_buffer, &result.index_memory);

    result.index_count = (uint32)total_indices;
    result.data = data;

    free(vertices);
    free(indices);

    LOG("loaded model: %s (%u verts, %u indices)\n", path, (uint32)total_verts, (uint32)total_indices);

    return result;
}

void loadAllEntities()
{
    vulkan_state.loaded_models[MODEL_3D_BOX - MODEL_3D_VOID] = loadModel("data/assets/rock.glb");
	vulkan_state.loaded_models[MODEL_3D_MIRROR 	- MODEL_3D_VOID] = loadModel("data/assets/mirror.glb");
    vulkan_state.loaded_models[MODEL_3D_GLASS - MODEL_3D_VOID] = loadModel("data/assets/glass.glb");
    vulkan_state.loaded_models[MODEL_3D_WIN_BLOCK - MODEL_3D_VOID] = loadModel("data/assets/flower.glb");

    vulkan_state.loaded_models[MODEL_3D_WATER - MODEL_3D_VOID] = loadModel("data/assets/water.glb");
    vulkan_state.loaded_models[MODEL_3D_WATER_BOTTOM - MODEL_3D_VOID] = loadModel("data/assets/water-bottom.glb");

    vulkan_state.loaded_models[MODEL_3D_SOURCE_RED     - MODEL_3D_VOID] = loadModel("data/assets/red-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_GREEN   - MODEL_3D_VOID] = loadModel("data/assets/green-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_BLUE    - MODEL_3D_VOID] = loadModel("data/assets/blue-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_MAGENTA - MODEL_3D_VOID] = loadModel("data/assets/magenta-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_YELLOW  - MODEL_3D_VOID] = loadModel("data/assets/yellow-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_CYAN    - MODEL_3D_VOID] = loadModel("data/assets/cyan-source.glb");
    vulkan_state.loaded_models[MODEL_3D_SOURCE_WHITE   - MODEL_3D_VOID] = loadModel("data/assets/white-source.glb");

    vulkan_state.laser_cylinder_model = loadModel("data/assets/laser-cylinder.glb");
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
    uint32 min_image_count = surface_capabilities.minImageCount + 1;
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
    swapchain_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
        depth_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
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

    // scene color image + view
    {
        VkImageCreateInfo scene_color_image_ci = {0};
        scene_color_image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        scene_color_image_ci.imageType = VK_IMAGE_TYPE_2D;
        scene_color_image_ci.extent.width = vulkan_state.swapchain_extent.width;
        scene_color_image_ci.extent.height = vulkan_state.swapchain_extent.height;
        scene_color_image_ci.extent.depth = 1;
        scene_color_image_ci.mipLevels = 1;
        scene_color_image_ci.arrayLayers = 1;
        scene_color_image_ci.format = vulkan_state.swapchain_format;
        scene_color_image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        scene_color_image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        scene_color_image_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        scene_color_image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        scene_color_image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(vulkan_state.logical_device_handle, &scene_color_image_ci, 0, &vulkan_state.scene_color_image);

        VkMemoryRequirements scene_color_memory_requirements = {0};
        vkGetImageMemoryRequirements(vulkan_state.logical_device_handle, vulkan_state.scene_color_image, &scene_color_memory_requirements);

        VkMemoryAllocateInfo scene_color_alloc = {0};
        scene_color_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        scene_color_alloc.allocationSize = scene_color_memory_requirements.size;
        scene_color_alloc.memoryTypeIndex = findMemoryType(scene_color_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(vulkan_state.logical_device_handle, &scene_color_alloc, 0, &vulkan_state.scene_color_image_memory);
        vkBindImageMemory(vulkan_state.logical_device_handle, vulkan_state.scene_color_image, vulkan_state.scene_color_image_memory, 0);

        VkImageViewCreateInfo scene_color_view_ci = {0};
        scene_color_view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        scene_color_view_ci.image = vulkan_state.scene_color_image;
        scene_color_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        scene_color_view_ci.format = vulkan_state.swapchain_format;
        scene_color_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        scene_color_view_ci.subresourceRange.baseMipLevel = 0;
        scene_color_view_ci.subresourceRange.levelCount = 1;
        scene_color_view_ci.subresourceRange.baseArrayLayer = 0;
        scene_color_view_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(vulkan_state.logical_device_handle, &scene_color_view_ci, 0, &vulkan_state.scene_color_image_view);
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

    // update scene color descriptor sets
    VkDescriptorImageInfo scene_color_desc_info = {0};
    scene_color_desc_info.sampler = vulkan_state.pixel_art_sampler;
    scene_color_desc_info.imageView = vulkan_state.scene_color_image_view;
    scene_color_desc_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet scene_color_desc_write = {0};
    scene_color_desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    scene_color_desc_write.dstSet = vulkan_state.scene_color_descriptor_set;
    scene_color_desc_write.dstBinding = 0;
    scene_color_desc_write.descriptorCount = 1;
    scene_color_desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    scene_color_desc_write.pImageInfo = &scene_color_desc_info;

    vkUpdateDescriptorSets(vulkan_state.logical_device_handle, 1, &scene_color_desc_write, 0, 0);

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

    // water frame buffers
    vulkan_state.water_framebuffers = realloc(vulkan_state.water_framebuffers, sizeof(VkFramebuffer) * vulkan_state.swapchain_image_count);

    for (uint32 i = 0; i < vulkan_state.swapchain_image_count; i++)
    {
        VkImageView water_fb_attachments[2] = { vulkan_state.swapchain_image_views[i], vulkan_state.depth_image_view };

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
    // overlay frame buffers
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
    raster->depthBiasEnable = VK_TRUE;
    raster->depthBiasConstantFactor = 0.0f;
    raster->depthBiasClamp = 0.0f;
    raster->depthBiasSlopeFactor = 0.0f;
}

void vulkanInitialize(RendererPlatformHandles platform_handles, DisplayInfo display)
{
    vulkan_display = display;

    vulkan_state.platform_handles = platform_handles;
    vulkan_state.vulkan_instance_handle = VK_NULL_HANDLE;
    vulkan_state.surface_handle = VK_NULL_HANDLE;
    vulkan_state.physical_device_handle = VK_NULL_HANDLE;

    // lists extensions we will need, in the creation process:
    // VK_KHR_surface: required to present to a window
    // VK_KHR_win32_surface: win32 binding for WSI
    // (surface = bridge between vulkan and platform)
    const char* instance_extensions[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };

	VkApplicationInfo api_info = {0};
    api_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    api_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_creation_info = {0}; // struct that holds creation instructions
    instance_creation_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_creation_info.pApplicationInfo = &api_info;
    instance_creation_info.enabledExtensionCount = 2;
    instance_creation_info.ppEnabledExtensionNames = instance_extensions;

    vkCreateInstance(&instance_creation_info, 0, &vulkan_state.vulkan_instance_handle);

    // struct that holds info that the surfaces uses to talk to platform layer
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
    // same engine. for now, i will only have one queue, so i'll put its priority at 1.0f
	float queue_priorities[1] = { 1.0f };

    // struct to get x queues from family y, with these priorities.
    // this struct is one per family i want queues from, so i will have one or two of these
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

    // logical device = opened session on a chosen GPU. need to pick queue families
    // + queues, device extensions, and device features.

    // we will need to pass the device extensions we want the logical device to use
    const char* device_extensions[] = { "VK_KHR_swapchain" };

    VkPhysicalDeviceFeatures device_features = {0};
    device_features.fillModeNonSolid = VK_TRUE;
    device_features.wideLines = VK_TRUE;

    VkDeviceCreateInfo device_info = {0}; // struct that bundles everthing the driver needs to create the logical device
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = (graphics_present_families_same ? 1u : 2u);
    device_info.pQueueCreateInfos = queue_family_infos;
    device_info.enabledExtensionCount = 1;
	device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &device_features;

    vkCreateDevice(vulkan_state.physical_device_handle, &device_info, 0, &vulkan_state.logical_device_handle);

    vulkan_state.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;

    // TODO: organise this better. the normal attachment is under 'first render pass' here.
    // first render pass
    {
        VkAttachmentDescription color_attachment = {0};
        color_attachment.format = chosen_surface_format.format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT; // no multi-sampling anti-aliasing, so only one color sample
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // start each frame by clearing the swapchain image to a solid color
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // we want the image to be read by the present engine after the render pass
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
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
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
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

    // water render pass to sample depth. also test depth but don't write
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

        water_attachments[1].format = vulkan_state.depth_format;
        water_attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        water_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        water_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_NONE;
        water_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        water_attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_NONE;
        water_attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        water_attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference water_color_ref = {0};
        water_color_ref.attachment = 0;
        water_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference water_depth_ref = {0};
        water_depth_ref.attachment = 1;
        water_depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkSubpassDescription water_subpass = {0};
        water_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        water_subpass.colorAttachmentCount = 1;
        water_subpass.pColorAttachments = &water_color_ref;
        water_subpass.pDepthStencilAttachment = &water_depth_ref;

        VkSubpassDependency water_dependency = {0};
        water_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        water_dependency.dstSubpass = 0;
        water_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        water_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        water_dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        water_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo water_rp_ci = {0};
        water_rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        water_rp_ci.attachmentCount = 2;
        water_rp_ci.pAttachments = water_attachments;
        water_rp_ci.subpassCount = 1;
        water_rp_ci.pSubpasses = &water_subpass;
        water_rp_ci.dependencyCount = 1;
        water_rp_ci.pDependencies = &water_dependency;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &water_rp_ci, 0, &vulkan_state.water_render_pass);
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
        overlay_attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        overlay_attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference overlay_color_reference = {0};
        overlay_color_reference.attachment = 0;
        overlay_color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference overlay_depth_reference = {0};
        overlay_depth_reference.attachment = 1;
        overlay_depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription overlay_subpass = {0};
        overlay_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        overlay_subpass.colorAttachmentCount = 1;
        overlay_subpass.pColorAttachments = &overlay_color_reference;
        overlay_subpass.pDepthStencilAttachment = &overlay_depth_reference;

        VkSubpassDependency overlay_dependency = {0};
        overlay_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        overlay_dependency.dstSubpass = 0;
        overlay_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        overlay_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        overlay_dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        overlay_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo overlay_rp_ci = {0};
        overlay_rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        overlay_rp_ci.attachmentCount = 2;
        overlay_rp_ci.pAttachments = overlay_attachments;
        overlay_rp_ci.subpassCount = 1;
        overlay_rp_ci.pSubpasses = &overlay_subpass;
        overlay_rp_ci.dependencyCount = 1;
        overlay_rp_ci.pDependencies = &overlay_dependency;

        vkCreateRenderPass(vulkan_state.logical_device_handle, &overlay_rp_ci, 0, &vulkan_state.overlay_render_pass);
    }

	// a framebuffer is the binding of the render pass' attachment slots to specific image views, with a fixed size (width/height) and layer count. 
    // it doesn't allocate memory, it just ties the render pass to the actual image 
    // one framebuffer per swapchan image view, because each acquired image is a different underlying image view

	VkCommandPoolCreateInfo command_pool_creation_info = {0}; // describes the command pool tied to graphics queue family
	command_pool_creation_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_creation_info.queueFamilyIndex = vulkan_state.graphics_family_index;
    command_pool_creation_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // lets us reset / rerecord command buffers

	vkCreateCommandPool(vulkan_state.logical_device_handle, &command_pool_creation_info, 0, &vulkan_state.graphics_command_pool_handle);

    // a semaphore is a GPU-GPU sync primitive. it has two states, signaled and not.
    // its signaled by the GPU as part of a queue operation, and waited by the GPU
    // in another queue operation
    
    // a fence is a CPU-GPU sync primitive. it also has two states, signaled and not.
    // its signaled by the GPU when a submission associated with the fence finishes.
    // the CPU can wait on it an reset it - they are the way the CPU knows if the GPU is done.

    // for a semaphore, 'signaled' means the GPU operation finished, so another GPU queue that
    // waits on it may proceed immediately. binary semaphores auto-reset to unsignaled when a
    // wait consumes them, so no manual reset here

    // for a fence, 'signaled' means the GPU finished the submission tied to that fence.
    // the CPU can wait on it; if it's signaled, the wait returns immediately.
    // note that fences stay signaled until manually reset (vkResetFences)

    vulkan_state.frames_in_flight = 1;
	vulkan_state.current_frame = 0;
	vulkan_state.image_available_semaphores = malloc(sizeof(VkSemaphore) * vulkan_state.frames_in_flight);
    vulkan_state.render_finished_semaphores = malloc(sizeof(VkSemaphore) * vulkan_state.frames_in_flight);
    vulkan_state.in_flight_fences = malloc(sizeof(VkFence) * vulkan_state.frames_in_flight);
											
    // struct that tells vulkan what kind of semaphore you want (binary)
    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // struct that defines the kind of fence you want (default)
    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	// semaphore and fence count is decided by frames_in_flight - this is because we want one set 
    // of sync objects per frame slot, and this is because the point of multiple frames_in_flight 
    // is having multiple acquires / submits, which cannot be handled by a single semaphore / fence.

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

    // LOADING SHADER MODULES

    VkShaderModule cube_vert_smh = {0};
    VkShaderModule cube_frag_smh = {0};
    VkShaderModule outline_vert_smh = {0};
    VkShaderModule outline_frag_smh = {0};
    VkShaderModule laser_fill_vert_smh = {0};
    VkShaderModule laser_fill_frag_smh = {0};
    VkShaderModule laser_outline_vert_smh = {0};
    VkShaderModule laser_outline_frag_smh = {0};
    VkShaderModule sprite_vert_smh = {0};
    VkShaderModule sprite_frag_smh = {0};
    VkShaderModule model_vert_smh = {0};
    VkShaderModule model_frag_smh = {0};
    VkShaderModule outline_post_vert_smh = {0};
    VkShaderModule outline_post_frag_smh = {0};
    VkShaderModule water_vert_smh = {0};
    VkShaderModule water_frag_smh = {0};

    VkPipelineShaderStageCreateInfo cube_vert_stage_ci 	 	     = loadShaderStage("data/shaders/spirv/tri.vert.spv", 	  	  	  &cube_vert_smh, 	  		 VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo cube_frag_stage_ci 	 	     = loadShaderStage("data/shaders/spirv/tri.frag.spv", 	  	  	  &cube_frag_smh, 	  		 VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo outline_vert_stage_ci 	     = loadShaderStage("data/shaders/spirv/outline.vert.spv", 	  	  &outline_vert_smh, 		 VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo outline_frag_stage_ci 	     = loadShaderStage("data/shaders/spirv/outline.frag.spv", 	  	  &outline_frag_smh, 		 VK_SHADER_STAGE_FRAGMENT_BIT);
	VkPipelineShaderStageCreateInfo laser_fill_vert_stage_ci     = loadShaderStage("data/shaders/spirv/laser-fill.vert.spv",      &laser_fill_vert_smh,   	 VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo laser_fill_frag_stage_ci     = loadShaderStage("data/shaders/spirv/laser-fill.frag.spv",      &laser_fill_frag_smh,   	 VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo laser_outline_vert_stage_ci  = loadShaderStage("data/shaders/spirv/laser-outline.vert.spv",   &laser_outline_vert_smh,   VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo laser_outline_frag_stage_ci  = loadShaderStage("data/shaders/spirv/laser-outline.frag.spv",   &laser_outline_frag_smh,   VK_SHADER_STAGE_FRAGMENT_BIT);
	VkPipelineShaderStageCreateInfo sprite_vert_stage_ci  	     = loadShaderStage("data/shaders/spirv/sprite.vert.spv",  	  	  &sprite_vert_smh,  		 VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo sprite_frag_stage_ci  	     = loadShaderStage("data/shaders/spirv/sprite.frag.spv",  	  	  &sprite_frag_smh,  		 VK_SHADER_STAGE_FRAGMENT_BIT);
	VkPipelineShaderStageCreateInfo model_vert_stage_ci 	 	 = loadShaderStage("data/shaders/spirv/model.vert.spv",   	  	  &model_vert_smh,   		 VK_SHADER_STAGE_VERTEX_BIT);
	VkPipelineShaderStageCreateInfo model_frag_stage_ci 	 	 = loadShaderStage("data/shaders/spirv/model.frag.spv",   	  	  &model_frag_smh,   		 VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo outline_post_vert_stage_ci   = loadShaderStage("data/shaders/spirv/outline-post.vert.spv",    &outline_post_vert_smh,    VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo outline_post_frag_stage_ci   = loadShaderStage("data/shaders/spirv/outline-post.frag.spv",    &outline_post_frag_smh,    VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo water_vert_stage_ci          = loadShaderStage("data/shaders/spirv/water.vert.spv",           &water_vert_smh,           VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo water_frag_stage_ci          = loadShaderStage("data/shaders/spirv/water.frag.spv",           &water_frag_smh,           VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineShaderStageCreateInfo cube_shader_stages[2]  	 	     = { cube_vert_stage_ci,    	  cube_frag_stage_ci }; 
    VkPipelineShaderStageCreateInfo outline_shader_stages[2] 	     = { outline_vert_stage_ci, 	  outline_frag_stage_ci }; 
    VkPipelineShaderStageCreateInfo laser_shader_stages[2]   	     = { laser_fill_vert_stage_ci,    laser_fill_frag_stage_ci };
    VkPipelineShaderStageCreateInfo laser_outline_shader_stages[2]   = { laser_outline_vert_stage_ci, laser_outline_frag_stage_ci };
    VkPipelineShaderStageCreateInfo sprite_shader_stages[2]  	     = { sprite_vert_stage_ci,  	  sprite_frag_stage_ci };
   	VkPipelineShaderStageCreateInfo model_shader_stages[2]   	     = { model_vert_stage_ci,   	  model_frag_stage_ci };
    VkPipelineShaderStageCreateInfo outline_post_shader_stages[2]    = { outline_post_vert_stage_ci,  outline_post_frag_stage_ci };
    VkPipelineShaderStageCreateInfo water_shader_stages[2]           = { water_vert_stage_ci,         water_frag_stage_ci };

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
    vertex_input_simple.pVertexAttributeDescriptions = vertex_attributes_simple;

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

    // empty vertex input for outline post render
    VkPipelineVertexInputStateCreateInfo empty_vertex_input = {0};
    empty_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // struct that describes how vertices are assembled into primatives before rasterization
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_creation_info = {0}; 
    input_assembly_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state_creation_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state_creation_info.primitiveRestartEnable = VK_FALSE;

    // a viewport maps from clip space ([-1, 1]) to a rectangle in framebuffer pixels (i.e. window coords)
    // a scissor is another pixel-space rectangle; fragments outside it are discarded
    
    VkViewport dummy_viewport = {0}; // placeholders to satisfy the pointer requirement; actual values are ignored because we'll set viewport at drawtime
    VkRect2D dummy_scissor = {0};

    VkPipelineViewportStateCreateInfo viewport_state_creation_info = {0}; // describes viewport for the pipeline. set at drawtime.
    viewport_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_creation_info.viewportCount = 1;
    viewport_state_creation_info.scissorCount = 1;
	viewport_state_creation_info.pViewports = &dummy_viewport;
    viewport_state_creation_info.pScissors = &dummy_scissor;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };

    VkPipelineDynamicStateCreateInfo dynamic_state_creation_info = {0};
    dynamic_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_creation_info.dynamicStateCount = (uint32)(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
    dynamic_state_creation_info.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo rasterization_state_creation_info = {0};
    rasterization_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_creation_info.depthClampEnable = VK_FALSE; // keep vertices outside the near / far range clipped (discarded)
    rasterization_state_creation_info.rasterizerDiscardEnable = VK_FALSE; // don't discard primitives; we want to rasterize
    rasterization_state_creation_info.polygonMode = VK_POLYGON_MODE_FILL; // fill triangles (not lines / points)
    rasterization_state_creation_info.cullMode = VK_CULL_MODE_NONE; // turn off back/face culling: skip rasterizing triangles facing a certain way
    rasterization_state_creation_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // define which winding would be "front"; doesn't matter while cull is off
    rasterization_state_creation_info.depthBiasEnable = VK_TRUE; 
    rasterization_state_creation_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_creation_info.depthBiasClamp = 0.0f;
    rasterization_state_creation_info.depthBiasSlopeFactor = 0.0f;
    rasterization_state_creation_info.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state_creation_info = {0};
    multisample_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_creation_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // no multisampling yet
    multisample_state_creation_info.sampleShadingEnable = VK_FALSE; // disable per-sample shading
    multisample_state_creation_info.minSampleShading = 1.0f;
    multisample_state_creation_info.pSampleMask = 0; // use all samples (default mask)
    multisample_state_creation_info.alphaToCoverageEnable = VK_FALSE;
    multisample_state_creation_info.alphaToOneEnable = VK_FALSE;

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

    VkSamplerCreateInfo sampler_creation_info = {0};
    sampler_creation_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; // TODO: double check all this info at some point
    sampler_creation_info.magFilter = VK_FILTER_NEAREST;
    sampler_creation_info.minFilter = VK_FILTER_NEAREST;
    sampler_creation_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_creation_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_creation_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_creation_info.anisotropyEnable = VK_FALSE;
    sampler_creation_info.maxAnisotropy = 1.0f;
    sampler_creation_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_creation_info.unnormalizedCoordinates = VK_FALSE;
    sampler_creation_info.compareEnable = VK_FALSE;
    sampler_creation_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_creation_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_creation_info.mipLodBias = 0.0f;
    sampler_creation_info.minLod = 0.0f;
    sampler_creation_info.maxLod = 0.0f;

    vkCreateSampler(vulkan_state.logical_device_handle, &sampler_creation_info, 0, &vulkan_state.pixel_art_sampler);

    // descriptor sets let the fragment shader look up the texture it needs from gpu memory. one descriptor set per texture in texture cache.
	VkDescriptorSetLayoutBinding descriptor_set_binding = {0};
	descriptor_set_binding.binding = 0;
    descriptor_set_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_set_binding.descriptorCount = 1;
    descriptor_set_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_creation_info = {0};
    descriptor_set_layout_creation_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_creation_info.bindingCount = 1;
    descriptor_set_layout_creation_info.pBindings = &descriptor_set_binding;

    vkCreateDescriptorSetLayout(vulkan_state.logical_device_handle, &descriptor_set_layout_creation_info, 0, &vulkan_state.descriptor_set_layout);

    // descriptor pool allocates memory for all descriptor sets
    VkDescriptorPoolSize descriptor_pool_size = {0};
    descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_pool_size.descriptorCount = 1024;
    
    VkDescriptorPoolCreateInfo descriptor_pool_creation_info = {0};
    descriptor_pool_creation_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_creation_info.poolSizeCount = 1;
    descriptor_pool_creation_info.pPoolSizes = &descriptor_pool_size;
    descriptor_pool_creation_info.maxSets = 1024;

    vkCreateDescriptorPool(vulkan_state.logical_device_handle, &descriptor_pool_creation_info, 0, &vulkan_state.descriptor_pool);

    // TODO: maybe wrap this, often writing this code
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

	VkDescriptorSetAllocateInfo scene_color_descriptor_set_alloc = {0};
    scene_color_descriptor_set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    scene_color_descriptor_set_alloc.descriptorPool = vulkan_state.descriptor_pool;
    scene_color_descriptor_set_alloc.descriptorSetCount = 1;
    scene_color_descriptor_set_alloc.pSetLayouts = &vulkan_state.descriptor_set_layout;
    vkAllocateDescriptorSets(vulkan_state.logical_device_handle, &scene_color_descriptor_set_alloc, &vulkan_state.scene_color_descriptor_set);

	createSwapchainResources();

	// CREATE CUBE (INSTANCED) PIPELINE LAYOUT

    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(InstancedPushConstants);

        VkPipelineLayoutCreateInfo layout_info = {0};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &vulkan_state.descriptor_set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_info, 0, &vulkan_state.cube_pipeline_layout);
    }

    // CREATE OUTLINE PIPELINE LAYOUT

    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset     = 0;
        push_constant_range.size       = (uint32)sizeof(PushConstants);
		
        VkPipelineLayoutCreateInfo outline_pipeline_layout_ci = {0};
        outline_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        outline_pipeline_layout_ci.setLayoutCount = 0;
        outline_pipeline_layout_ci.pSetLayouts = 0;
        outline_pipeline_layout_ci.pushConstantRangeCount = 1;
        outline_pipeline_layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &outline_pipeline_layout_ci, 0, &vulkan_state.outline_pipeline_layout);
    }

    // CREATE MODEL PIPELINE LAYOUT

    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(PushConstants);

        VkPipelineLayoutCreateInfo model_pipeline_layout_ci = {0};
        model_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        model_pipeline_layout_ci.setLayoutCount = 1;
        model_pipeline_layout_ci.pSetLayouts = &vulkan_state.descriptor_set_layout;
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

        VkDescriptorSetLayout post_layouts[2] = { vulkan_state.descriptor_set_layout, vulkan_state.descriptor_set_layout };

        VkPipelineLayoutCreateInfo layout_ci = {0};
        layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_ci.setLayoutCount = 2;
        layout_ci.pSetLayouts = post_layouts;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &layout_ci, 0, &vulkan_state.outline_post_pipeline_layout);
    }

    // CREATE WATER PIPELINE LAYOUT

    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(WaterPushConstants);

        VkDescriptorSetLayout water_set_layouts[3] = { vulkan_state.descriptor_set_layout, vulkan_state.descriptor_set_layout, vulkan_state.descriptor_set_layout };

        VkPipelineLayoutCreateInfo water_pipeline_layout_ci = {0};
        water_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        water_pipeline_layout_ci.setLayoutCount = 3;
        water_pipeline_layout_ci.pSetLayouts = water_set_layouts;
        water_pipeline_layout_ci.pushConstantRangeCount = 1;
        water_pipeline_layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &water_pipeline_layout_ci, 0, &vulkan_state.water_pipeline_layout);
    }

    // CREATE LASER PIPELINE LAYOUT

    {
		VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = (uint32)sizeof(PushConstants);

        VkPipelineLayoutCreateInfo laser_pipeline_layout_ci = {0};
        laser_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        laser_pipeline_layout_ci.setLayoutCount = 0;
        laser_pipeline_layout_ci.pSetLayouts = 0;
        laser_pipeline_layout_ci.pushConstantRangeCount = 1;
        laser_pipeline_layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &laser_pipeline_layout_ci, 0, &vulkan_state.laser_pipeline_layout);
    }

	// SPRITE PIPELINE LAYOUT

    {
        VkPushConstantRange push_constant_range = {0};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset     = 0;
        push_constant_range.size       = (uint32)sizeof(PushConstants); 

        VkPipelineLayoutCreateInfo sprite_pipeline_layout_ci= {0};
        sprite_pipeline_layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        sprite_pipeline_layout_ci.setLayoutCount = 1;
        sprite_pipeline_layout_ci.pSetLayouts = &vulkan_state.descriptor_set_layout; 
        sprite_pipeline_layout_ci.pushConstantRangeCount = 1;
        sprite_pipeline_layout_ci.pPushConstantRanges = &push_constant_range;

        vkCreatePipelineLayout(vulkan_state.logical_device_handle, &sprite_pipeline_layout_ci, 0, &vulkan_state.sprite_pipeline_layout);
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

    vulkan_state.atlas_2d_asset_index   = getOrLoadAsset((char*)ATLAS_2D_PATH);
    vulkan_state.atlas_font_asset_index = getOrLoadAsset((char*)ATLAS_FONT_PATH);
    vulkan_state.atlas_3d_asset_index   = getOrLoadAsset((char*)ATLAS_3D_PATH);

	// define instanced cube pipeline: depth on, blending off
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        blend_attachments[0] = color_blend_attachment_state;
        blend_attachments[1] = color_blend_attachment_state;
        
        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo cube_ci = base_graphics_pipeline_creation_info;
        cube_ci.pVertexInputState = &vertex_input_instanced;
        cube_ci.layout = vulkan_state.cube_pipeline_layout;
        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &cube_ci, 0, &vulkan_state.cube_pipeline_handle);
    }

    // define selected outline pipeline
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        blend_attachments[0] = color_blend_attachment_state;
        blend_attachments[1] = color_blend_attachment_state;

        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		rasterization_state_creation_info.polygonMode = VK_POLYGON_MODE_LINE;
        rasterization_state_creation_info.cullMode = VK_CULL_MODE_NONE;
        rasterization_state_creation_info.lineWidth = 1.0f;

		VkGraphicsPipelineCreateInfo outline_ci = base_graphics_pipeline_creation_info;
        outline_ci.pStages = outline_shader_stages;
        outline_ci.layout = vulkan_state.outline_pipeline_layout; // use outline pipeline layout (no descriptors)

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &outline_ci, 0, &vulkan_state.outline_pipeline_handle);
    }

    // define model pipeline: depth on, write to stencil 2.
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;

        blend_attachments[0] = color_blend_attachment_state;
        blend_attachments[1] = color_blend_attachment_state;

        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;

        depth_stencil_state_creation_info.stencilTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.front.failOp = VK_STENCIL_OP_KEEP;
        depth_stencil_state_creation_info.front.passOp = VK_STENCIL_OP_REPLACE;
        depth_stencil_state_creation_info.front.depthFailOp = VK_STENCIL_OP_KEEP;
        depth_stencil_state_creation_info.front.compareOp = VK_COMPARE_OP_ALWAYS;
        depth_stencil_state_creation_info.front.compareMask = 0xFF;
        depth_stencil_state_creation_info.front.writeMask = 0xFF;
        depth_stencil_state_creation_info.front.reference = 2;
        depth_stencil_state_creation_info.back = depth_stencil_state_creation_info.front;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterization_state_creation_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkGraphicsPipelineCreateInfo model_ci = base_graphics_pipeline_creation_info;
        model_ci.pStages = model_shader_stages;
        model_ci.layout = vulkan_state.model_pipeline_layout;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &model_ci, 0, &vulkan_state.model_pipeline_handle);
    }

    // define water pipeline (instanced)
    {
        VkPipelineColorBlendAttachmentState water_blend = {0};
        water_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        water_blend.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo water_blend_ci = {0};
        water_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        water_blend_ci.attachmentCount = 1;
        water_blend_ci.pAttachments = &water_blend;

        blend_attachments[0] = color_blend_attachment_state;
        blend_attachments[1] = color_blend_attachment_state;

        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil_state_creation_info.stencilTestEnable = VK_FALSE;
        rasterization_state_creation_info.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterization_state_creation_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkGraphicsPipelineCreateInfo water_ci = base_graphics_pipeline_creation_info;
        water_ci.pVertexInputState = &water_vertex_input;
        water_ci.pStages = water_shader_stages;
        water_ci.layout = vulkan_state.water_pipeline_layout;
        water_ci.renderPass = vulkan_state.water_render_pass;
        water_ci.pColorBlendState = &water_blend_ci;
        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &water_ci, 0, &vulkan_state.water_pipeline_handle);
    }

    // define outline post pipeline. different enough that we might as well set up an entirely new creation info. sets up state first, then assigns
    {
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

    // define laser fill pipeline (overlay render pass, after outlines)
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_TRUE;
        color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil_state_creation_info.stencilTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.front.passOp = VK_STENCIL_OP_REPLACE;
        depth_stencil_state_creation_info.front.compareOp = VK_COMPARE_OP_ALWAYS;
        depth_stencil_state_creation_info.front.writeMask = 0xFF;
        depth_stencil_state_creation_info.front.reference = 1;
        depth_stencil_state_creation_info.back = depth_stencil_state_creation_info.front;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_BACK_BIT;

        VkPipelineColorBlendAttachmentState laser_blend = color_blend_attachment_state;
        VkPipelineColorBlendStateCreateInfo laser_blend_ci = {0};
        laser_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        laser_blend_ci.attachmentCount = 1;
        laser_blend_ci.pAttachments = &laser_blend;

        VkGraphicsPipelineCreateInfo laser_ci = base_graphics_pipeline_creation_info;
        laser_ci.pStages = laser_shader_stages;
        laser_ci.layout = vulkan_state.laser_pipeline_layout;
        laser_ci.renderPass = vulkan_state.overlay_render_pass;
        laser_ci.pColorBlendState = &laser_blend_ci;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &laser_ci, 0, &vulkan_state.laser_fill_pipeline_handle);
    }

    // define laser outline pipeline (overlay render pass, after outlines)
    {
        resetPipelineStates(&color_blend_attachment_state, &depth_stencil_state_creation_info, &rasterization_state_creation_info);

        color_blend_attachment_state.blendEnable = VK_FALSE;
        /*
        color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
        */

        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil_state_creation_info.stencilTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.front.failOp = VK_STENCIL_OP_KEEP;
        depth_stencil_state_creation_info.front.passOp = VK_STENCIL_OP_KEEP;
        depth_stencil_state_creation_info.front.depthFailOp = VK_STENCIL_OP_KEEP;
        depth_stencil_state_creation_info.front.compareOp = VK_COMPARE_OP_EQUAL;
        depth_stencil_state_creation_info.front.compareMask = 0x01;
        depth_stencil_state_creation_info.front.writeMask = 0x00;
        depth_stencil_state_creation_info.front.reference = 0;
        depth_stencil_state_creation_info.back = depth_stencil_state_creation_info.front;

        rasterization_state_creation_info.cullMode = VK_CULL_MODE_NONE;
        rasterization_state_creation_info.polygonMode = VK_POLYGON_MODE_FILL;

        VkPipelineColorBlendAttachmentState laser_outline_blend = color_blend_attachment_state;
        VkPipelineColorBlendStateCreateInfo laser_outline_blend_ci = {0};
        laser_outline_blend_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        laser_outline_blend_ci.attachmentCount = 1;
        laser_outline_blend_ci.pAttachments = &laser_outline_blend;

        VkGraphicsPipelineCreateInfo laser_outline_ci = base_graphics_pipeline_creation_info;
        laser_outline_ci.pStages = laser_outline_shader_stages;
        laser_outline_ci.layout = vulkan_state.laser_pipeline_layout;
        laser_outline_ci.renderPass = vulkan_state.overlay_render_pass;
        laser_outline_ci.pColorBlendState = &laser_outline_blend_ci;

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &laser_outline_ci, 0, &vulkan_state.laser_outline_pipeline_handle);
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

        vkCreateGraphicsPipelines(vulkan_state.logical_device_handle, VK_NULL_HANDLE, 1, &sprite_ci, 0, &vulkan_state.sprite_pipeline_handle);
    }

    createInstanceBuffer(&vulkan_state.cube_instance_buffer, sizeof(CubeInstanceData) * CUBE_INSTANCE_CAPACITY, &vulkan_state.cube_instance_memory, &vulkan_state.cube_instance_mapped);
    createInstanceBuffer(&vulkan_state.water_instance_buffer, sizeof(WaterInstanceData) * WATER_INSTANCE_CAPACITY, &vulkan_state.water_instance_memory, &vulkan_state.water_instance_mapped);

    loadAllEntities();
}

void vulkanSubmitFrame(DrawCommand* draw_commands, int32 draw_command_count, float global_time, Camera game_camera, ShaderMode shader_mode_from_game)
{  
    vulkan_camera = game_camera;

    shader_mode = shader_mode_from_game;

    water_time = global_time;

    sprite_instance_count = 0;
    cube_instance_count = 0;
    outline_instance_count = 0;
    laser_instance_count = 0;
    model_instance_count = 0;
    model_selected_outline_instance_count = 0;
    water_instance_count = 0;

    for (int asset_index = 0; asset_index < draw_command_count; asset_index++)
    {
        DrawCommand* command = &draw_commands[asset_index];
        SpriteId sprite_id = command->sprite_id;
        AssetType type = command->type;

        if (type == OUTLINE_3D)
        {
            bool render_model = true;
            if (sprite_id >= MODEL_3D_VOID && sprite_id <= MODEL_3D_SOURCE_WHITE) render_model = false;
            if (vulkan_state.loaded_models[sprite_id - MODEL_3D_VOID].index_count <= 0) render_model = false;
            if (render_model)
            {
                Model* model = &model_selected_outline_instances[model_selected_outline_instance_count++];
                model->model_id = (uint32)sprite_id;
                model->coords   = command->coords;
                model->scale    = command->scale;
                model->rotation = command->rotation;
            }
            else
            {
                // outline 3d called with cube id, so render cube
                Cube* cube = &outline_instances[outline_instance_count++];
                cube->coords      = command->coords;
                cube->scale       = command->scale;
                cube->rotation    = command->rotation;
                cube->uv          = (Vec4){ 0, 0, 1, 1 };
                cube->asset_index = 0;
            }
        }
        else if (type == LASER)
        {
            Laser* laser = &laser_instances[laser_instance_count++];
            laser->center   = command->coords;
            laser->length   = command->scale.z;
            laser->rotation = command->rotation;
            laser->color    = command->color;
        }
        else if (type == MODEL_3D)
        {
            Model* model = &model_instances[model_instance_count++];
            model->model_id = (uint32)sprite_id;
            model->coords   = command->coords;
            model->scale    = command->scale;
            model->rotation = command->rotation;
        }
        else if (type == WATER_3D)
		{
            Water* water = &water_instances[water_instance_count++];
            water->coords = command->coords;
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
            sprite->alpha       = command->color.x;
            sprite->uv          = spriteUV(sprite_id, type, atlas_width, atlas_height);
        }
        else if (type == CUBE_3D)
        {
            Vec4 uv_rect = spriteUV(sprite_id, type, ATLAS_3D_WIDTH, ATLAS_3D_HEIGHT);

            Cube* cube = &cube_instances[cube_instance_count++];
            cube->asset_index = (uint32)vulkan_state.atlas_3d_asset_index;
            cube->coords      = command->coords;
            cube->scale       = command->scale;
            cube->rotation    = command->rotation;
            cube->uv          = uv_rect;
        }
    }

    // fill cube instance buffer
    CubeInstanceData* cube_gpu_instances = (CubeInstanceData*)vulkan_state.cube_instance_mapped;

    for (uint32 instance_index = 0; instance_index < cube_instance_count; instance_index++)
    {
        Cube* cube = &cube_instances[instance_index];
        mat4BuildTRS(cube_gpu_instances[instance_index].model, cube->coords, cube->rotation, cube->scale);
        cube_gpu_instances[instance_index].uv_rect = cube->uv;
    }

    VkMappedMemoryRange cube_flush_range = {0};
    cube_flush_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    cube_flush_range.memory = vulkan_state.cube_instance_memory;
    cube_flush_range.offset = 0;
    cube_flush_range.size = VK_WHOLE_SIZE;
    vkFlushMappedMemoryRanges(vulkan_state.logical_device_handle, 1, &cube_flush_range);

    // fill water instance buffer
    WaterInstanceData* water_gpu_instances = (WaterInstanceData*)vulkan_state.water_instance_mapped;
    for (uint32 i = 0; i < water_instance_count; i++)
    {
        Water* water = &water_instances[i];
        Vec4 rotation = { 0, 0, 0, 1 };
        Vec3 scale = { 1, 1, 1 };
        mat4BuildTRS(water_gpu_instances[i].model, water->coords, rotation, scale);
    }
    VkMappedMemoryRange water_flush_range = {0};
    water_flush_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    water_flush_range.memory = vulkan_state.water_instance_memory;
    water_flush_range.offset = 0;
    water_flush_range.size = VK_WHOLE_SIZE;
    vkFlushMappedMemoryRanges(vulkan_state.logical_device_handle, 1, &water_flush_range);
}

void vulkanDraw(void)
{
    // THROTTLE TO N FRAMES IN FLIGHT

    // blocks until the previous GPU submission that used this slot has finised. if GPU is still using that slot, CPU must wait (i.e. you cannot get more than N frames ahead)
	vkWaitForFences(vulkan_state.logical_device_handle, 1, &vulkan_state.in_flight_fences[vulkan_state.current_frame], VK_TRUE, UINT64_MAX);

    // ACQUIRE A SWAPCHAIN IMAGE

	uint32 swapchain_image_index = 0;
    // picks which swapchain image you may render to next, and tells the GPU when the image is ready via a semaphore
    VkResult acquire_result = vkAcquireNextImageKHR(vulkan_state.logical_device_handle, vulkan_state.swapchain_handle, UINT64_MAX, vulkan_state.image_available_semaphores[vulkan_state.current_frame], VK_NULL_HANDLE, &swapchain_image_index);

	switch (acquire_result)
    {
        case VK_SUCCESS: break;
        case VK_SUBOPTIMAL_KHR: break;
        case VK_ERROR_OUT_OF_DATE_KHR: return; // TODO: trigger swapchain recreate before drawing
        case VK_ERROR_SURFACE_LOST_KHR: return; // TODO: recreate surface (then swapchain)
        default: return;
    }

    // PER-IMAGE FENCE BOOK-KEEPING

	if (vulkan_state.images_in_flight[swapchain_image_index] != VK_NULL_HANDLE)
    {
        vkWaitForFences(vulkan_state.logical_device_handle, 1, &vulkan_state.images_in_flight[swapchain_image_index], VK_TRUE, UINT64_MAX);
    }

    vulkan_state.images_in_flight[swapchain_image_index] = vulkan_state.in_flight_fences[vulkan_state.current_frame]; // record that this frame-slot's fence now owns this image.
                                                                                                                            // next time this image is acquired, we'll wait on this fence if needed
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // GPU needs to know what pipeline stage is blocked until the semaphore signals.

    vkResetFences(vulkan_state.logical_device_handle, 1, &vulkan_state.in_flight_fences[vulkan_state.current_frame]);

    // actual things happening

    VkCommandBuffer command_buffer = vulkan_state.swapchain_command_buffers[swapchain_image_index]; // select command buffer that corresponds to the acquired swapchain image.
    vkResetCommandBuffer(command_buffer, 0); // throw away last frame's commands for this image and start fresh.

	VkCommandBufferBeginInfo command_buffer_begin_info = {0};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // submit once, reset next frame
	vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

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

    VkRenderPassBeginInfo render_pass_begin_info = {0};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = vulkan_state.render_pass_handle;
    render_pass_begin_info.framebuffer = vulkan_state.swapchain_framebuffers[swapchain_image_index];
    render_pass_begin_info.renderArea.offset = (VkOffset2D){ 0,0 };
    render_pass_begin_info.renderArea.extent = vulkan_state.swapchain_extent;
    render_pass_begin_info.clearValueCount = 3;
    render_pass_begin_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    // dynamic pipeline state (same baked graphics pipeline, but change viewport / scissor whenever we change what's in frame. for now we don't really use this though)

    // compute 16:9 letterbox viewport
    float target_aspect = 16.0f / 9.0f;
    float window_width = (float)vulkan_state.swapchain_extent.width;
    float window_height = (float)vulkan_state.swapchain_extent.height;
    float window_aspect = window_width / window_height;

    float viewport_width, viewport_height, viewport_x, viewport_y;
    if (window_aspect > target_aspect)
    {
        // window is wider than 16:9: bars on sides
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
    viewport.height = -viewport_height; // negative for y-up
    viewport.x = viewport_x;
    viewport.y = viewport_y + viewport_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset.x = (int32)viewport_x;
    scissor.offset.y = (int32)viewport_y;
    scissor.extent.width = (uint32)viewport_width;
    scissor.extent.height = (uint32)viewport_height;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    float aspect = target_aspect;
    float projection_matrix[16], view_matrix[16];
    mat4BuildPerspective(projection_matrix, vulkan_camera.fov * (6.283185f / 360.0f), aspect, 1.0f, 300.0f);
    mat4BuildViewFromQuat(view_matrix, vulkan_camera.coords, vulkan_camera.rotation);

	// CUBE PIPELINE (INSTANCED)

    if (cube_instance_count > 0)
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.cube_pipeline_handle);

        VkBuffer cube_buffers[2] = { vulkan_state.cube_vertex_buffer, vulkan_state.cube_instance_buffer };
        VkDeviceSize cube_offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(command_buffer, 0, 2, cube_buffers, cube_offsets);
        vkCmdBindIndexBuffer(command_buffer, vulkan_state.cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.cube_pipeline_layout, 0, 1, &vulkan_state.descriptor_sets[vulkan_state.atlas_3d_asset_index], 0, 0);

        InstancedPushConstants pc = {0};
        memcpy(pc.view, view_matrix, sizeof(pc.view));
        memcpy(pc.proj, projection_matrix, sizeof(pc.proj));

        vkCmdPushConstants(command_buffer, vulkan_state.cube_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(InstancedPushConstants), &pc);

        vkCmdDrawIndexed(command_buffer, vulkan_state.cube_index_count, cube_instance_count, 0, 0, 0);
    }

    // (CUBE) OUTLINE PIPELINE

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.outline_pipeline_handle);
    
    VkDeviceSize outline_vb_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vulkan_state.cube_vertex_buffer, &outline_vb_offset);
    vkCmdBindIndexBuffer(command_buffer, vulkan_state.cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);

    // same camera
    for (uint32 outline_instance_index = 0; outline_instance_index < outline_instance_count; outline_instance_index++)
    {
        Cube* cube = &outline_instances[outline_instance_index];
        float model_matrix[16];
        mat4BuildTRS(model_matrix, cube->coords, cube->rotation, cube->scale);

        PushConstants pc = {0};
        memcpy(pc.model, model_matrix,      sizeof(pc.model));
        memcpy(pc.view,  view_matrix,       sizeof(pc.view));
        memcpy(pc.proj,  projection_matrix, sizeof(pc.proj));
        pc.uv_rect = (Vec4){0,0,1,1};

        vkCmdPushConstants(command_buffer, vulkan_state.outline_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

		vkCmdSetDepthBias(command_buffer, -0.1f, 0.0f, -0.1f);
        vkCmdDrawIndexed(command_buffer, vulkan_state.cube_index_count, 1, 0, 0, 0);
		vkCmdSetDepthBias(command_buffer, 0.0f, 0.0f, 0.0f);
    }

    // MODEL PIPELINE (unbatched, stencil-per-object outlines)

    if (model_instance_count > 0)
    {
        for (uint32 model_instance_index = 0; model_instance_index < model_instance_count; model_instance_index++)
        {
            Model* model = &model_instances[model_instance_index];
            LoadedModel* model_data = &vulkan_state.loaded_models[model->model_id - MODEL_3D_VOID];
            if (model_data->index_count == 0) continue;

            VkDeviceSize offset = 0;
            float model_matrix[16];
            mat4BuildTRS(model_matrix, model->coords, model->rotation, model->scale);

            PushConstants pc = {0};
            memcpy(pc.model, model_matrix,      sizeof(pc.model));
            memcpy(pc.view,  view_matrix,       sizeof(pc.view));
            memcpy(pc.proj,  projection_matrix, sizeof(pc.proj));
            pc.uv_rect = (Vec4){0, 0, 1, 1};

            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.model_pipeline_handle);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.model_pipeline_layout, 0, 1, &vulkan_state.descriptor_sets[vulkan_state.atlas_3d_asset_index], 0, 0);
            vkCmdBindVertexBuffers(command_buffer, 0, 1, &model_data->vertex_buffer, &offset);
            vkCmdBindIndexBuffer(command_buffer, model_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(command_buffer, vulkan_state.model_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(command_buffer, model_data->index_count, 1, 0, 0, 0);
        }
    }

    // MODEL SELECTED OUTLINES

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.outline_pipeline_handle);

    for (uint32 outline_index = 0; outline_index < model_selected_outline_instance_count; outline_index++)
    {
        Model* model = &model_selected_outline_instances[outline_index];
        LoadedModel* model_data = &vulkan_state.loaded_models[model->model_id - MODEL_3D_VOID];
        if (model_data->index_count == 0) continue;

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &model_data->vertex_buffer, &offset);
        vkCmdBindIndexBuffer(command_buffer, model_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);

        float model_matrix[16];
        mat4BuildTRS(model_matrix, model->coords, model->rotation, model->scale);

        PushConstants pc = {0};
        memcpy(pc.model, model_matrix,      sizeof(pc.model));
        memcpy(pc.view,  view_matrix,       sizeof(pc.view));
        memcpy(pc.proj,  projection_matrix, sizeof(pc.proj));
        pc.uv_rect = (Vec4){0, 0, 1, 1};

        vkCmdPushConstants(command_buffer, vulkan_state.outline_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

		vkCmdSetDepthBias(command_buffer, -0.1f, 0.0f, -0.1f);
        vkCmdDrawIndexed(command_buffer, model_data->index_count, 1, 0, 0, 0);
		vkCmdSetDepthBias(command_buffer, 0.0f, 0.0f, 0.0f);
    }

    vkCmdEndRenderPass(command_buffer);

    if (shader_mode != OLD)
    {
        // transition depth from attachment to shader read
        VkImageMemoryBarrier depth_to_read = {0};
        depth_to_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depth_to_read.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_to_read.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depth_to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_to_read.image = vulkan_state.depth_image;
        depth_to_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        depth_to_read.subresourceRange.baseMipLevel = 0;
        depth_to_read.subresourceRange.levelCount = 1;
        depth_to_read.subresourceRange.baseArrayLayer = 0;
        depth_to_read.subresourceRange.layerCount = 1;
        depth_to_read.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depth_to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &depth_to_read);

        // outline post pass
        {
            VkRenderPassBeginInfo post_rp_begin = {0};
            post_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            post_rp_begin.renderPass = vulkan_state.outline_post_render_pass;
            post_rp_begin.framebuffer = vulkan_state.outline_post_framebuffers[swapchain_image_index];
            post_rp_begin.renderArea.offset = (VkOffset2D){0, 0};
            post_rp_begin.renderArea.extent = vulkan_state.swapchain_extent;
            post_rp_begin.clearValueCount = 0;

            vkCmdBeginRenderPass(command_buffer, &post_rp_begin, VK_SUBPASS_CONTENTS_INLINE);

            // use full window viewport for post pass, not letterboxed
            VkViewport post_viewport = {0};
            post_viewport.width = (float)vulkan_state.swapchain_extent.width;
            post_viewport.height = (float)vulkan_state.swapchain_extent.height;
            post_viewport.x = 0;
            post_viewport.y = 0;
            post_viewport.minDepth = 0.0f;
            post_viewport.maxDepth = 1.0f;

            VkRect2D post_scissor = {0};
            post_scissor.extent = vulkan_state.swapchain_extent;

            vkCmdSetViewport(command_buffer, 0, 1, &post_viewport);
            vkCmdSetScissor(command_buffer, 0, 1, &post_scissor);

            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.outline_post_pipeline);
            VkDescriptorSet post_sets[2] = { vulkan_state.depth_descriptor_set, vulkan_state.normal_descriptor_set };
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.outline_post_pipeline_layout, 0, 2, post_sets, 0, 0);

            float focal_length = (float)vulkan_state.swapchain_extent.height / ((2 * 3.141592653f) * tanf(vulkan_camera.fov / 360.0f));

            float post_pc[6] = {
                1.0f / (float)vulkan_state.swapchain_extent.width,
                1.0f / (float)vulkan_state.swapchain_extent.height,
                2.0f, // depth threshold
                0.2f, // normal threshold
                focal_length,
                (shader_mode == OUTLINE_TEST) ? 1.0f : 0.0f
            };
            vkCmdPushConstants(command_buffer, vulkan_state.outline_post_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 6, post_pc);

            vkCmdDraw(command_buffer, 3, 1, 0, 0); // fullscreen triangle
        }

        vkCmdEndRenderPass(command_buffer);
    }

    // COPY SWAPCHAIN COLOR TO SCENE COLOR IMAGE FOR WATER TO SAMPLE

    {
        VkImageMemoryBarrier pre_copy_barriers[2] = {0};

        // swapchain: PRESENT_SRC -> TRANSFER_SRC
        pre_copy_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        pre_copy_barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        pre_copy_barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        pre_copy_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_copy_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_copy_barriers[0].image = vulkan_state.swapchain_images[swapchain_image_index];
        pre_copy_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        pre_copy_barriers[0].subresourceRange.baseMipLevel = 0;
        pre_copy_barriers[0].subresourceRange.levelCount = 1;
        pre_copy_barriers[0].subresourceRange.baseArrayLayer = 0;
        pre_copy_barriers[0].subresourceRange.layerCount = 1;
        pre_copy_barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        pre_copy_barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        // scene color: UNDEFINED -> TRANSFER_DST
        pre_copy_barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        pre_copy_barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        pre_copy_barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        pre_copy_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_copy_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre_copy_barriers[1].image = vulkan_state.scene_color_image;
        pre_copy_barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        pre_copy_barriers[1].subresourceRange.baseMipLevel = 0;
        pre_copy_barriers[1].subresourceRange.levelCount = 1;
        pre_copy_barriers[1].subresourceRange.baseArrayLayer = 0;
        pre_copy_barriers[1].subresourceRange.layerCount = 1;
        pre_copy_barriers[1].srcAccessMask = 0;
        pre_copy_barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 2, pre_copy_barriers);

        // the actual copy
        VkImageCopy copy_region = {0};
        copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.srcSubresource.layerCount = 1;
        copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.dstSubresource.layerCount = 1;
        copy_region.extent.width = vulkan_state.swapchain_extent.width;
        copy_region.extent.height = vulkan_state.swapchain_extent.height;
        copy_region.extent.depth = 1;

        vkCmdCopyImage(command_buffer, vulkan_state.swapchain_images[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vulkan_state.scene_color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        // post-copy transitions
        VkImageMemoryBarrier post_copy_barriers[2] = {0};

        // swapchain: TRANSFER_SRC -> PRESENT_SRC (overlay pass expects this)
        post_copy_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        post_copy_barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        post_copy_barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        post_copy_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post_copy_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post_copy_barriers[0].image = vulkan_state.swapchain_images[swapchain_image_index];
        post_copy_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        post_copy_barriers[0].subresourceRange.baseMipLevel = 0;
        post_copy_barriers[0].subresourceRange.levelCount = 1;
        post_copy_barriers[0].subresourceRange.baseArrayLayer = 0;
        post_copy_barriers[0].subresourceRange.layerCount = 1;
        post_copy_barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        post_copy_barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // scene color: TRANSFER_DST -> SHADER_READ_ONLY
        post_copy_barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        post_copy_barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        post_copy_barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        post_copy_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post_copy_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post_copy_barriers[1].image = vulkan_state.scene_color_image;
        post_copy_barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        post_copy_barriers[1].subresourceRange.baseMipLevel = 0;
        post_copy_barriers[1].subresourceRange.levelCount = 1;
        post_copy_barriers[1].subresourceRange.baseArrayLayer = 0;
        post_copy_barriers[1].subresourceRange.layerCount = 1;
        post_copy_barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        post_copy_barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 2, post_copy_barriers);
    }

    // WATER PASS (INSTANCED)
    if (water_instance_count > 0)
    {
        LoadedModel* water_data = &vulkan_state.loaded_models[MODEL_3D_WATER - MODEL_3D_VOID];
        if (water_data->index_count > 0)
        {
            VkRenderPassBeginInfo water_rp_begin = {0};
            water_rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            water_rp_begin.renderPass = vulkan_state.water_render_pass;
            water_rp_begin.framebuffer = vulkan_state.water_framebuffers[swapchain_image_index];
            water_rp_begin.renderArea.offset = (VkOffset2D){0, 0};
            water_rp_begin.renderArea.extent = vulkan_state.swapchain_extent;
            water_rp_begin.clearValueCount = 0;
            vkCmdBeginRenderPass(command_buffer, &water_rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(command_buffer, 0, 1, &viewport);
            vkCmdSetScissor(command_buffer, 0, 1, &scissor);
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.water_pipeline_handle);
            VkDescriptorSet water_sets[3] = { 
                vulkan_state.descriptor_sets[vulkan_state.atlas_3d_asset_index], 
                vulkan_state.scene_color_descriptor_set, 
                vulkan_state.depth_descriptor_set 
            };
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.water_pipeline_layout, 0, 3, water_sets, 0, 0);

            VkBuffer water_buffers[2] = { water_data->vertex_buffer, vulkan_state.water_instance_buffer };
            VkDeviceSize water_offsets[2] = { 0, 0 };
            vkCmdBindVertexBuffers(command_buffer, 0, 2, water_buffers, water_offsets);
            vkCmdBindIndexBuffer(command_buffer, water_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);

            WaterPushConstants pc = {0};
            memcpy(pc.view, view_matrix, sizeof(pc.view));
            memcpy(pc.proj, projection_matrix, sizeof(pc.proj));
            pc.time = water_time;
            vkCmdPushConstants(command_buffer, vulkan_state.water_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterPushConstants), &pc);

            vkCmdDrawIndexed(command_buffer, water_data->index_count, water_instance_count, 0, 0, 0);
            vkCmdEndRenderPass(command_buffer);
        }
	}

    // transition depth back to attachment for overlay pass (lasers, which affect outlines)

    VkImageMemoryBarrier depth_to_attachment = {0};
    depth_to_attachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depth_to_attachment.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth_to_attachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_to_attachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depth_to_attachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depth_to_attachment.image = vulkan_state.depth_image;
    depth_to_attachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    depth_to_attachment.subresourceRange.baseMipLevel = 0;
    depth_to_attachment.subresourceRange.levelCount = 1;
    depth_to_attachment.subresourceRange.baseArrayLayer = 0;
    depth_to_attachment.subresourceRange.layerCount = 1;
    depth_to_attachment.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depth_to_attachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, 0, 0, 0, 1, &depth_to_attachment);

    // overlay pass: water and lasers (outline + fill)
    {
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

        // LASERS
        LoadedModel* laser_mesh = &vulkan_state.laser_cylinder_model;
        if (laser_mesh->index_count > 0)
        {
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.laser_fill_pipeline_handle);

            VkDeviceSize laser_vb_offset = 0;
            vkCmdBindVertexBuffers(command_buffer, 0, 1, &laser_mesh->vertex_buffer, &laser_vb_offset);
            vkCmdBindIndexBuffer(command_buffer, laser_mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);

            if (shader_mode != OUTLINE_TEST)
            {
                for (uint32 laser_index = 0; laser_index < laser_instance_count; laser_index++)
                {
                    Laser* laser = &laser_instances[laser_index];
                    Vec3 laser_scale = { 1.0f, 1.0f, laser->length };

                    float model_matrix[16];
                    mat4BuildTRS(model_matrix, laser->center, laser->rotation, laser_scale);

                    LaserPushConstants pc = {0};
                    memcpy(pc.model, model_matrix, sizeof(pc.model));
                    memcpy(pc.view, view_matrix, sizeof(pc.view));
                    memcpy(pc.proj, projection_matrix, sizeof(pc.proj));
                    pc.color = (Vec4){ laser->color.x, laser->color.y, laser->color.z, 1.0f };

                    vkCmdPushConstants(command_buffer, vulkan_state.laser_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LaserPushConstants), &pc);
                    vkCmdDrawIndexed(command_buffer, laser_mesh->index_count, 1, 0, 0, 0);
                }

                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.laser_outline_pipeline_handle);

                vkCmdBindVertexBuffers(command_buffer, 0, 1, &laser_mesh->vertex_buffer, &laser_vb_offset);
                vkCmdBindIndexBuffer(command_buffer, laser_mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);

                for (uint32 laser_index = 0; laser_index < laser_instance_count; laser_index++)
                {
                    Laser* laser = &laser_instances[laser_index];
                    Vec3 laser_scale = { 1.0f, 1.0f, laser->length };

                    float model_matrix[16];
                    mat4BuildTRS(model_matrix, laser->center, laser->rotation, laser_scale);

                    LaserPushConstants push_constants = {0};
                    memcpy(push_constants.model, model_matrix, sizeof(push_constants.model));
                    memcpy(push_constants.view, view_matrix, sizeof(push_constants.view));
                    memcpy(push_constants.proj, projection_matrix, sizeof(push_constants.proj));
                    push_constants.color = (Vec4){ laser->color.x, laser->color.y, laser->color.z, 1.0f };

                    vkCmdPushConstants(command_buffer, vulkan_state.laser_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LaserPushConstants), &push_constants);
                    vkCmdDrawIndexed(command_buffer, laser_mesh->index_count, 1, 0, 0, 0);
                }
            }
        }
    }

    // overlay pass: sprites
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline_handle); 

        VkDeviceSize sprite_vb_offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &vulkan_state.sprite_vertex_buffer, &sprite_vb_offset);
        vkCmdBindIndexBuffer(command_buffer, vulkan_state.sprite_index_buffer, 0, VK_INDEX_TYPE_UINT32);

        float ortho[16], view2d[16];
        mat4BuildOrtho(ortho, 0.0f, (float)vulkan_state.swapchain_extent.width, 0.0f, (float)vulkan_state.swapchain_extent.height, 0.0f, 1.0f);
        mat4Identity(view2d);

        int32 last_sprite_asset = -1;

        for (uint32 sprite_instance_index = 0; sprite_instance_index < sprite_instance_count; sprite_instance_index++)
        {
            Sprite* sprite = &sprite_instances[sprite_instance_index];

            if ((int32)sprite->asset_index != last_sprite_asset)
            {
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_state.sprite_pipeline_layout, 0, 1, &vulkan_state.descriptor_sets[sprite->asset_index], 0, 0);
                last_sprite_asset = (int32)sprite->asset_index;
            }

            float model_matrix[16];
            Vec4 identity_quaternion = { 0, 0, 0, 1}; // TODO: make global
            mat4BuildTRS(model_matrix, sprite->coords, identity_quaternion, sprite->size);

            PushConstants push_constants = {0};
            memcpy(push_constants.model, model_matrix, sizeof(push_constants.model));
            memcpy(push_constants.view,  view2d, 	   sizeof(push_constants.view));
            memcpy(push_constants.proj,  ortho, 	   sizeof(push_constants.proj));
            push_constants.uv_rect = sprite->uv;
            push_constants.alpha = sprite->alpha;

            vkCmdPushConstants(command_buffer, vulkan_state.sprite_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);

            vkCmdDrawIndexed(command_buffer, vulkan_state.sprite_index_count, 1, 0, 0, 0);
        }
    }

    vkCmdEndRenderPass(command_buffer);

    vkEndCommandBuffer(command_buffer);

	first_submit_since_draw = true;

    // SUBMIT THE PRE-RECORDED CB FOR THAT IMAGE

    VkSubmitInfo submit_info = {0}; // container for the GPU submission: which semaphores to wait on, which command buffer to execute, and which semaphore to signal when done.
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
        // fallback: signal the fence with an empty submit so the next frame won't hang
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
        case VK_ERROR_SURFACE_LOST_KHR: return; // surface died; recreate surface (then swapchain)
        default: return;
    }

    // ADVANCE TO NEXT FRAME SLOT

	vulkan_state.current_frame = (vulkan_state.current_frame + 1) % vulkan_state.frames_in_flight;
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

    // destroy scene color view + image
    vkDestroyImageView(vulkan_state.logical_device_handle, vulkan_state.scene_color_image_view, 0);
    vkDestroyImage(vulkan_state.logical_device_handle, vulkan_state.scene_color_image, 0);
    vkFreeMemory(vulkan_state.logical_device_handle, vulkan_state.scene_color_image_memory, 0);

    // old swapchain is destroyed inside createSwapchainResources
    createSwapchainResources();
}

void vulkanShutdown(void)
{

}
