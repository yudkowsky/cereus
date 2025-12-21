#define VK_USE_PLATFORM_WIN32_KHR
#define STB_IMAGE_IMPLEMENTATION

#include <stdio.h>
#include "win32_renderer_bridge.h"
#include <vulkan/vulkan.h>
#include "stb_image.h"

const Int2 SCREEN_RESOLUTION = { 1920, 1080 };

// TODO(spike): set these in loadAsset where stb_image gives me width / height. store in CachedAsset.
const int32 ATLAS_2D_WIDTH = 128;
const int32 ATLAS_2D_HEIGHT = 128;
const int32 ATLAS_FONT_WIDTH = 120;
const int32 ATLAS_FONT_HEIGHT = 180;
const int32 ATLAS_3D_WIDTH = 480;
const int32 ATLAS_3D_HEIGHT = 320;

const char* ATLAS_2D_PATH = "w:/cereus/data/sprites/atlas-2d.png";
const char* ATLAS_FONT_PATH = "w:/cereus/data/sprites/atlas-font.png";
const char* ATLAS_3D_PATH = "w:/cereus/data/sprites/atlas-3d.png";

bool first_submit_since_draw = true;

typedef struct Vertex
{
    float x, y, z;
    float u, v;
    float r, g ,b;
}
Vertex;

typedef struct Sprite
{
    uint32 asset_index;
    Vec3 coords;
	Vec3 size;
    Vec4 uv;
}
Sprite;

typedef struct Cube 
{
	uint32 asset_index;
    Vec3 coords;
    Vec3 scale;
    Vec4 rotation;
    Vec4 uv;
}
Cube;

typedef struct CachedAsset
{
	VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    char path[256];
}
CachedAsset;

typedef struct PushConstants
{
    float model[16];
    float view[16];
    float proj[16];
    Vec4 uv_rect;
}
PushConstants;

typedef struct RendererState
{
    RendererPlatformHandles platform_handles;
    VkInstance vulkan_instance_handle;
    VkSurfaceKHR surface_handle;
	VkPhysicalDevice physical_device_handle;

    uint32 graphics_family_index;
    uint32 present_family_index;
    VkQueue graphics_queue_handle;
    VkQueue present_queue_handle;
    VkDevice logical_device_handle;

	VkSwapchainKHR swapchain_handle;
    uint32 swapchain_image_count;
    VkImageView* swapchain_image_views; // VkImageView is a view that tells vulkan how we intend to access that image: type, format, array layers, some other stuff.
	VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    VkRenderPass render_pass_handle;
    VkFramebuffer* swapchain_framebuffers;

    VkCommandPool graphics_command_pool_handle;
    VkCommandBuffer* swapchain_command_buffers;

	uint32 frames_in_flight; // how many frames the CPU is allowed to get ahead of the GPU
	uint32 current_frame;
    VkSemaphore* image_available_semaphores; // semaphore(s) that handle WSI -> graphics. wsi produces swapchain image, graphics queue renders into that image.
    VkSemaphore* render_finished_semaphores; // semaphore(s) that handle graphics -> present. once graphics finishes rendering, graphics sends renders to be presented.
    VkFence* in_flight_fences; 
    VkFence* images_in_flight; // for each swapchain image, when GPU finishes that submission, the fence signals; we store the fence to check if image is still in flight.

    //VkBuffer vertex_buffer_handle;
    //VkDeviceMemory vertex_memory;
    //void* mapped_vertex_pointer;
    VkShaderModule vertex_shader_module_handle;
    VkShaderModule fragment_shader_module_handle;
    VkPipelineLayout graphics_pipeline_layout; 
    VkPipeline cube_pipeline_handle;
    VkPipeline sprite_pipeline_handle;

    VkSampler pixel_art_sampler;
    CachedAsset asset_cache[256];
    uint32 asset_cache_count;

    int32 atlas_2d_asset_index;
	int32 atlas_font_asset_index;
    int32 atlas_3d_asset_index;

    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[1024];

    VkFormat depth_format;
    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;

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
}
RendererState;
RendererState renderer_state;

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

Vertex frame_vertex_stash[65536];
uint32 frame_vertex_count = 0;

Sprite sprite_instances[1024];
uint32 sprite_instance_count = 0;

Cube cube_instances[1024];
uint32 cube_instance_count = 0;

Camera renderer_camera = {0};

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
    vkGetPhysicalDeviceMemoryProperties(renderer_state.physical_device_handle, &memory_properties);
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
        return (int32)id - (int32)SPRITE_2D_COUNT;
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

    vkCreateBuffer(renderer_state.logical_device_handle, &buffer_info, 0, &staging_buffer);

    VkMemoryRequirements cpu_memory_requirements;
	vkGetBufferMemoryRequirements(renderer_state.logical_device_handle, staging_buffer, &cpu_memory_requirements);

    VkMemoryAllocateInfo cpu_memory_allocation_info = {0};
    cpu_memory_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    cpu_memory_allocation_info.allocationSize = cpu_memory_requirements.size;
    cpu_memory_allocation_info.memoryTypeIndex = findMemoryType(cpu_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(renderer_state.logical_device_handle, &cpu_memory_allocation_info, 0, &staging_buffer_memory);
    vkBindBufferMemory(renderer_state.logical_device_handle, staging_buffer, staging_buffer_memory, 0);

    void* gpu_memory_pointer; // starts as CPU pointer
    vkMapMemory(renderer_state.logical_device_handle, staging_buffer_memory, 0, image_size_bytes, 0, &gpu_memory_pointer); // data now points to GPU-visible memory
    memcpy(gpu_memory_pointer, pixels, (size_t)image_size_bytes); // writes from pixels to data
    vkUnmapMemory(renderer_state.logical_device_handle, staging_buffer_memory); // removes CPU access

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

    vkCreateImage(renderer_state.logical_device_handle, &image_info, 0, &texture_image);

	VkMemoryRequirements gpu_memory_requirements;
    vkGetImageMemoryRequirements(renderer_state.logical_device_handle, texture_image, &gpu_memory_requirements);

    VkMemoryAllocateInfo gpu_memory_allocation_info = {0};
    gpu_memory_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    gpu_memory_allocation_info.allocationSize = gpu_memory_requirements.size;
	gpu_memory_allocation_info.memoryTypeIndex = findMemoryType(gpu_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(renderer_state.logical_device_handle, &gpu_memory_allocation_info, 0, &texture_image_memory);
    vkBindImageMemory(renderer_state.logical_device_handle, texture_image, texture_image_memory, 0);

    VkCommandBufferAllocateInfo command_buffer_allocation_info = {0};
    command_buffer_allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocation_info.commandPool = renderer_state.graphics_command_pool_handle;
    command_buffer_allocation_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(renderer_state.logical_device_handle, &command_buffer_allocation_info, &command_buffer);

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

    vkQueueSubmit(renderer_state.graphics_queue_handle, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(renderer_state.graphics_queue_handle); // blocks until the GPU finishes 

    vkFreeCommandBuffers(renderer_state.logical_device_handle, renderer_state.graphics_command_pool_handle, 1, &command_buffer);
    vkDestroyBuffer(renderer_state.logical_device_handle, staging_buffer, 0);
    vkFreeMemory(renderer_state.logical_device_handle, staging_buffer_memory, 0);

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

    vkCreateImageView(renderer_state.logical_device_handle, &image_view_info, 0, &texture_image_view);

	VkDescriptorSetAllocateInfo descriptor_set_allocation_info = {0};
    descriptor_set_allocation_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_set_allocation_info.descriptorPool = renderer_state.descriptor_pool;
    descriptor_set_allocation_info.descriptorSetCount = 1;
    descriptor_set_allocation_info.pSetLayouts = &renderer_state.descriptor_set_layout;

    vkAllocateDescriptorSets(renderer_state.logical_device_handle, &descriptor_set_allocation_info, &renderer_state.descriptor_sets[renderer_state.asset_cache_count]);

    VkDescriptorImageInfo descriptor_image_info = {0};
    descriptor_image_info.sampler = renderer_state.pixel_art_sampler;
    descriptor_image_info.imageView = texture_image_view;
    descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet descriptor_set_write = {0};
    descriptor_set_write.sType= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_set_write.dstSet = renderer_state.descriptor_sets[renderer_state.asset_cache_count];
	descriptor_set_write.dstBinding = 0;
    descriptor_set_write.descriptorCount = 1;
	descriptor_set_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_set_write.pImageInfo = &descriptor_image_info;

    vkUpdateDescriptorSets(renderer_state.logical_device_handle, 1, &descriptor_set_write, 0, 0);

    renderer_state.asset_cache[renderer_state.asset_cache_count].image = texture_image;
    renderer_state.asset_cache[renderer_state.asset_cache_count].memory = texture_image_memory;
    renderer_state.asset_cache[renderer_state.asset_cache_count].view = texture_image_view;
    strcpy(renderer_state.asset_cache[renderer_state.asset_cache_count].path, path);

    renderer_state.asset_cache_count++;

    return (int32)(renderer_state.asset_cache_count - 1);
}

int32 getOrLoadAsset(char* path)
{
    // check if already loaded
    for (uint32 asset_cache_index = 0; asset_cache_index < renderer_state.asset_cache_count; asset_cache_index++)
    {
        if (strcmp(renderer_state.asset_cache[asset_cache_index].path, path) == 0) return (int32)asset_cache_index;
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

    vkCreateBuffer(renderer_state.logical_device_handle, &buffer_info, 0, &staging_buffer);

    VkMemoryRequirements memory_requirements = {0};
    vkGetBufferMemoryRequirements(renderer_state.logical_device_handle, staging_buffer, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(renderer_state.logical_device_handle, &alloc_info, 0, &staging_memory);
    vkBindBufferMemory(renderer_state.logical_device_handle, staging_buffer, staging_memory, 0);

    // copy CPU data into staging memory
    void* mapped = 0;
    vkMapMemory(renderer_state.logical_device_handle, staging_memory, 0, size, 0, &mapped);
    memcpy(mapped, source, (size_t)size);
    vkUnmapMemory(renderer_state.logical_device_handle, staging_memory);

    // create device-local buffer
    VkBuffer device_buffer = VK_NULL_HANDLE;
    VkDeviceMemory device_memory = VK_NULL_HANDLE;

    buffer_info = (VkBufferCreateInfo){0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | final_usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(renderer_state.logical_device_handle, &buffer_info, 0, &device_buffer);

    vkGetBufferMemoryRequirements(renderer_state.logical_device_handle, device_buffer, &memory_requirements);

    alloc_info = (VkMemoryAllocateInfo){0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(renderer_state.logical_device_handle, &alloc_info, 0, &device_memory);
    vkBindBufferMemory(renderer_state.logical_device_handle, device_buffer, device_memory, 0);

    // record + submit copy command
    VkCommandBufferAllocateInfo cb_alloc = {0};
    cb_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_alloc.commandPool = renderer_state.graphics_command_pool_handle;
    cb_alloc.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(renderer_state.logical_device_handle, &cb_alloc, &command_buffer);

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

    vkQueueSubmit(renderer_state.graphics_queue_handle, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(renderer_state.graphics_queue_handle);

    vkFreeCommandBuffers(renderer_state.logical_device_handle, renderer_state.graphics_command_pool_handle, 1, &command_buffer);

    // cleanup and return
    vkDestroyBuffer(renderer_state.logical_device_handle, staging_buffer, 0);
    vkFreeMemory(renderer_state.logical_device_handle, staging_memory, 0);

    *out_buffer = device_buffer;
    *out_memory = device_memory;
}

void rendererInitialise(RendererPlatformHandles platform_handles)
{
    renderer_state.platform_handles = platform_handles;
    renderer_state.vulkan_instance_handle = VK_NULL_HANDLE;
    renderer_state.surface_handle = VK_NULL_HANDLE;
    renderer_state.physical_device_handle = VK_NULL_HANDLE;

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

    vkCreateInstance(&instance_creation_info, 0, &renderer_state.vulkan_instance_handle);

    // struct that holds info that the surfaces uses to talk to platform layer
	VkWin32SurfaceCreateInfoKHR surface_creation_info = {0};
    surface_creation_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_creation_info.hinstance = renderer_state.platform_handles.module_handle;
    surface_creation_info.hwnd = renderer_state.platform_handles.window_handle;

	vkCreateWin32SurfaceKHR(renderer_state.vulkan_instance_handle, &surface_creation_info, 0, &renderer_state.surface_handle);

	uint32 device_count = 0;
    vkEnumeratePhysicalDevices(renderer_state.vulkan_instance_handle, &device_count, 0);

	if (device_count == 0) return;

	VkPhysicalDevice* physical_devices = malloc(sizeof(*physical_devices) * device_count);
	vkEnumeratePhysicalDevices(renderer_state.vulkan_instance_handle, &device_count, physical_devices);
	
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
			vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[device_increment], family_increment, renderer_state.surface_handle, &can_present);

            if (local_present_family_index == -1 && can_present) local_present_family_index = (int)family_increment;

            // NOTE(spike): some inconsistencies with where properties are checked, but this is
            // 				still safe, just a bit less readable. also avoids redoing some checks.
            if (local_graphics_family_index != -1 && local_present_family_index != -1)
            {
                uint32 device_extension_count = 0;
                vkEnumerateDeviceExtensionProperties(physical_devices[device_increment], 0, &device_extension_count, 0);
                VkExtensionProperties* extensions = malloc(sizeof(*extensions) * device_extension_count); // NOTE(spike): may be malloc(0)
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
                    renderer_state.physical_device_handle = physical_devices[device_increment];
                    renderer_state.graphics_family_index = (uint32)local_graphics_family_index;
                    renderer_state.present_family_index = (uint32)local_present_family_index;
                }
                break;
           	}
        }
        free(families);
		
        if (renderer_state.physical_device_handle != VK_NULL_HANDLE) break;
    }
    free(physical_devices);

    uint32 present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(renderer_state.physical_device_handle, renderer_state.surface_handle, &present_mode_count, 0);
    
    if (present_mode_count == 0) return;

    VkPresentModeKHR* present_modes = malloc(sizeof(*present_modes) * present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(renderer_state.physical_device_handle, renderer_state.surface_handle, &present_mode_count, present_modes);

    VkPresentModeKHR chosen_present_mode = VK_PRESENT_MODE_FIFO_KHR; // FIFO guaranteed - will now overwrite with MAILBOX if possible

    for (uint32 present_mode_increment = 0; present_mode_increment < present_mode_count; present_mode_increment++)
    {
		if (present_modes[present_mode_increment] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            chosen_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    free(present_modes);

    VkSurfaceCapabilitiesKHR surface_capabilities = {0}; // constraints and options for this device/surface pair, reported by WSI.
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer_state.physical_device_handle, renderer_state.surface_handle, &surface_capabilities);

    uint32 surface_format_count = 0; // the allowed (pixel format, color space) pairs for images to present.
    vkGetPhysicalDeviceSurfaceFormatsKHR(renderer_state.physical_device_handle, renderer_state.surface_handle, &surface_format_count, 0);

	if (surface_format_count == 0) return;

    VkSurfaceFormatKHR* surface_formats = malloc(sizeof(*surface_formats) * surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(renderer_state.physical_device_handle, renderer_state.surface_handle, &surface_format_count, surface_formats);

    VkSurfaceFormatKHR chosen_surface_format = surface_formats[0]; // some random guaranteed - will now overwrite if possible

	// TODO(spike): handle edge case where only one surface format, equal to VK_FORMAT_UNDEFINED
    for (uint32 surface_format_increment = 0; surface_format_increment < surface_format_count; surface_format_increment++)
    {
		if (surface_formats[surface_format_increment].format == VK_FORMAT_B8G8R8A8_SRGB && surface_formats[surface_format_increment].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosen_surface_format = surface_formats[surface_format_increment];
            break;
        }
    }
    free(surface_formats);
	
    renderer_state.swapchain_format = chosen_surface_format.format;

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
    graphics_queue_info.queueFamilyIndex = renderer_state.graphics_family_index;
    graphics_queue_info.queueCount = 1;
    graphics_queue_info.pQueuePriorities = queue_priorities;

    VkDeviceQueueCreateInfo queue_family_infos[2] = { graphics_queue_info };

    // check if we need a separate VkDeviceQueueCreateInfo for present capabilities;
    // i.e., if present family differs from graphics family, and so we have two queues
    bool graphics_present_families_same = (renderer_state.present_family_index == renderer_state.graphics_family_index);

    if (!graphics_present_families_same)
    {
        VkDeviceQueueCreateInfo present_queue_info = {0};
        present_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        present_queue_info.queueFamilyIndex = renderer_state.present_family_index;
        present_queue_info.queueCount = 1;
        present_queue_info.pQueuePriorities = queue_priorities;

        queue_family_infos[1] = present_queue_info;
    }

    // logical device = opened session on a chosen GPU. need to pick queue families
    // + queues, device extensions, and device features.

    // we will need to pass the device extensions we want the logical device to use
    const char* device_extensions[] = { "VK_KHR_swapchain" };

    VkPhysicalDeviceFeatures device_features = {0}; // struct where we enable core VkPhysicalDeviceFeatures - don't need for now

    VkDeviceCreateInfo device_info = {0}; // struct that bundles everthing the driver needs to create the logical device
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = (graphics_present_families_same ? 1u : 2u);
    device_info.pQueueCreateInfos = queue_family_infos;
    device_info.enabledExtensionCount = 1;
	device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &device_features;

    vkCreateDevice(renderer_state.physical_device_handle, &device_info, 0, &renderer_state.logical_device_handle);

    VkExtent2D chosen_extent = surface_capabilities.currentExtent;

    if (chosen_extent.width == UINT32_MAX) // if UINT32_MAX here, no size is set, so we have to grab window dimensions ourselves
    {
		RECT window_rect = {0};
        GetClientRect(renderer_state.platform_handles.window_handle, &window_rect); // TODO(spike): why does this function call work?? secret <windows.h> hiding somewhere?
		uint32 window_width = (uint32)(window_rect.right - window_rect.left);
		uint32 window_height = (uint32)(window_rect.bottom - window_rect.top);

        if (window_width < surface_capabilities.minImageExtent.width) window_width = surface_capabilities.minImageExtent.width;
        if (window_width > surface_capabilities.maxImageExtent.width) window_width = surface_capabilities.maxImageExtent.width;
        if (window_height < surface_capabilities.minImageExtent.height) window_height = surface_capabilities.minImageExtent.height;
        if (window_height > surface_capabilities.maxImageExtent.height) window_height = surface_capabilities.maxImageExtent.height;

        chosen_extent.width = window_width;
        chosen_extent.height = window_height;
    }

    renderer_state.swapchain_extent = chosen_extent; // only assign after selection so we always have true data in renderer_state

	// depth image creation for 3d rendering

    renderer_state.depth_format = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo depth_image_info = {0};
    depth_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_image_info.imageType = VK_IMAGE_TYPE_2D;
    depth_image_info.extent.width = renderer_state.swapchain_extent.width;
    depth_image_info.extent.height = renderer_state.swapchain_extent.height;
    depth_image_info.extent.depth = 1;
    depth_image_info.mipLevels = 1;
    depth_image_info.arrayLayers = 1;
    depth_image_info.format = renderer_state.depth_format;
    depth_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateImage(renderer_state.logical_device_handle, &depth_image_info, 0, &renderer_state.depth_image);
	
    VkMemoryRequirements depth_image_memory_requirements;
    vkGetImageMemoryRequirements(renderer_state.logical_device_handle, renderer_state.depth_image, &depth_image_memory_requirements);

	VkMemoryAllocateInfo depth_memory_allocation_info = {0};
    depth_memory_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depth_memory_allocation_info.allocationSize = depth_image_memory_requirements.size;
    depth_memory_allocation_info.memoryTypeIndex = findMemoryType(depth_image_memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(renderer_state.logical_device_handle, &depth_memory_allocation_info, 0, &renderer_state.depth_image_memory);

    vkBindImageMemory(renderer_state.logical_device_handle, renderer_state.depth_image, renderer_state.depth_image_memory, 0);

    VkImageViewCreateInfo depth_view_info = {0};
    depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depth_view_info.image = renderer_state.depth_image;
    depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_view_info.format = renderer_state.depth_format;
    depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_view_info.subresourceRange.baseMipLevel = 0;
    depth_view_info.subresourceRange.levelCount = 1;
    depth_view_info.subresourceRange.baseArrayLayer = 0;
    depth_view_info.subresourceRange.layerCount = 1;

    vkCreateImageView(renderer_state.logical_device_handle, &depth_view_info, 0, &renderer_state.depth_image_view);

    uint32 minimum_swapchain_image_count = surface_capabilities.minImageCount + 1; // may be different from actual count (asking for minimum when creating swapchain)

    // if maxImageCount = 0, then this means 'no maximum', so no need to clamp.
    if (surface_capabilities.maxImageCount != 0 && minimum_swapchain_image_count > surface_capabilities.maxImageCount) minimum_swapchain_image_count = surface_capabilities.maxImageCount;

    uint32 queue_family_indices[2] = { renderer_state.graphics_family_index, renderer_state.present_family_index }; // only used if concurrent sharing mode is enabled

	// the swapchain = the WSI-owned pool of images to be presented tied to your window. creating it
    // tells the driver what kind of images to allocate, and how they will be scheduled / used

    VkSwapchainCreateInfoKHR swapchain_creation_info = {0}; // struct to tell WSI exactly what backbuffer pool to get
    swapchain_creation_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_creation_info.surface = renderer_state.surface_handle;
    swapchain_creation_info.minImageCount = minimum_swapchain_image_count;
	swapchain_creation_info.imageFormat = chosen_surface_format.format;
	swapchain_creation_info.imageColorSpace = chosen_surface_format.colorSpace;
    swapchain_creation_info.imageExtent = chosen_extent;
	swapchain_creation_info.imageArrayLayers = 1; // 1 layer per image - win32 swapchains are single-view (no multi-view)
	swapchain_creation_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_creation_info.presentMode = chosen_present_mode;
    swapchain_creation_info.preTransform = surface_capabilities.currentTransform;
    swapchain_creation_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // TODO(spike): should set this as something available from my surface_capabilities.supportedCompositeAlpha
   	swapchain_creation_info.clipped = VK_TRUE;
    swapchain_creation_info.oldSwapchain = VK_NULL_HANDLE;

    if (graphics_present_families_same) 
    {
        swapchain_creation_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        // queueFamilyIndexCount and pQueueFamilyIndices are ignored on exclusive sharing mode
    } 
    else 
    {
		swapchain_creation_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_creation_info.queueFamilyIndexCount = 2;
		swapchain_creation_info.pQueueFamilyIndices = queue_family_indices;
    }

	vkCreateSwapchainKHR(renderer_state.logical_device_handle, &swapchain_creation_info, 0, &renderer_state.swapchain_handle);

	vkGetSwapchainImagesKHR(renderer_state.logical_device_handle, renderer_state.swapchain_handle, &renderer_state.swapchain_image_count, 0);
    VkImage* swapchain_images = malloc(sizeof(*swapchain_images) * renderer_state.swapchain_image_count);
    vkGetSwapchainImagesKHR(renderer_state.logical_device_handle, renderer_state.swapchain_handle, &renderer_state.swapchain_image_count, swapchain_images);

    VkImageViewCreateInfo view_creation_info = {0};
    view_creation_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_creation_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_creation_info.format = chosen_surface_format.format;
    view_creation_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY; // how the R/G/B/A channels are read from the image
    view_creation_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY; // a swizzle map tells vulkan how to rewrite R/G/B/A channels when the image is read through the view.
    view_creation_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY; // we just want to keep them as is - other situations might want to reorder (e.g. BGRA image -> RGBA ordering)
    view_creation_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_creation_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // which aspects of the image the view exposes: color only
	view_creation_info.subresourceRange.baseMipLevel = 0; // mipmaps are prefiltered, downscaled versions of the image. we just have the full-res image, so 0 downscaling...
	view_creation_info.subresourceRange.levelCount = 1; // ...and so we only have the one level.
	view_creation_info.subresourceRange.baseArrayLayer = 0; // many vulkan images can have array layers (e.g. a cube with 6) - this says view starts at array 0 (the only array) 

    renderer_state.swapchain_image_views = malloc(sizeof(VkImageView) * renderer_state.swapchain_image_count);

    for (uint32 swapchain_image_increment = 0; swapchain_image_increment < renderer_state.swapchain_image_count; swapchain_image_increment++)
    {
        view_creation_info.image = swapchain_images[swapchain_image_increment];
    	vkCreateImageView(renderer_state.logical_device_handle, &view_creation_info, 0, &renderer_state.swapchain_image_views[swapchain_image_increment]);
    }

	free(swapchain_images);

	// a render pass is vulkan's contract for how to use images (attachments) - 'single color attachment' = we
    // have just just one render target (the swapchain image) and don't do depth / stencil.

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
    depth_attachment.format = renderer_state.depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
	
    VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

    VkRenderPassCreateInfo render_pass_creation_info = {0}; // container that ties attachment(s), subpass(es), and dependency(ies) into a single render pass object.
	render_pass_creation_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_creation_info.attachmentCount = 2; 
    render_pass_creation_info.pAttachments = attachments;
    render_pass_creation_info.subpassCount = 1;
	render_pass_creation_info.pSubpasses = &color_output_subpass;
    render_pass_creation_info.dependencyCount = 1;
    render_pass_creation_info.pDependencies = &color_output_subpass_dependency; // same story here - just a pointer to our one dependency, rather than an array.

    vkCreateRenderPass(renderer_state.logical_device_handle, &render_pass_creation_info, 0, &renderer_state.render_pass_handle);

	// a framebuffer is the binding of the render pass' attachment slots to specific image views, with a fixed size (width/height) and layer count. 
    // it doesn't allocate memory, it just ties the render pass to the actual image 
    // one framebuffer per swapchan image view, because each acquired image is a different underlying image view

    renderer_state.swapchain_framebuffers = malloc(sizeof(VkFramebuffer) * renderer_state.swapchain_image_count);

    VkFramebufferCreateInfo framebuffer_creation_info = {0};
    framebuffer_creation_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_creation_info.renderPass = renderer_state.render_pass_handle;
    framebuffer_creation_info.width = renderer_state.swapchain_extent.width;
    framebuffer_creation_info.height = renderer_state.swapchain_extent.height;
    framebuffer_creation_info.layers = 1;

    for (uint32 swapchain_image_index = 0; swapchain_image_index < renderer_state.swapchain_image_count; swapchain_image_index++)
    {
		VkImageView framebuffer_attachments[2] = { renderer_state.swapchain_image_views[swapchain_image_index], renderer_state.depth_image_view };
        framebuffer_creation_info.attachmentCount = 2;
        framebuffer_creation_info.pAttachments = framebuffer_attachments;

        vkCreateFramebuffer(renderer_state.logical_device_handle, &framebuffer_creation_info, 0, &renderer_state.swapchain_framebuffers[swapchain_image_index]);
    }

	VkCommandPoolCreateInfo command_pool_creation_info = {0}; // describes the command pool tied to graphics queue family
	command_pool_creation_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_creation_info.queueFamilyIndex = renderer_state.graphics_family_index;
    command_pool_creation_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // lets us reset / rerecord command buffers

	vkCreateCommandPool(renderer_state.logical_device_handle, &command_pool_creation_info, 0, &renderer_state.graphics_command_pool_handle);

    renderer_state.swapchain_command_buffers = malloc(sizeof(VkCommandBuffer) * renderer_state.swapchain_image_count);

	VkCommandBufferAllocateInfo command_buffer_allocation_info = {0}; // which command pool (memory) to allocate from; how many command buffers to allocate in one shot
    command_buffer_allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocation_info.commandPool = renderer_state.graphics_command_pool_handle;
    command_buffer_allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // primary command buffers submit directly to the graphics queue. secondary CBs are for nesting / multithreading 
    command_buffer_allocation_info.commandBufferCount = renderer_state.swapchain_image_count;
	
    vkAllocateCommandBuffers(renderer_state.logical_device_handle, &command_buffer_allocation_info, renderer_state.swapchain_command_buffers);

	VkCommandBufferBeginInfo command_buffer_begin_info = {0}; // container for how a command buffer begins recording (unused for us, no flags needed)
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkClearValue clear_color = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } }; // isn't actually used
    
	VkRenderPassBeginInfo render_pass_begin_info = {0}; // describes which render pass, framebuffer, render area, and clear values to use when we begin the render pass
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = renderer_state.render_pass_handle;
    render_pass_begin_info.renderArea.offset.x = 0;
    render_pass_begin_info.renderArea.offset.y = 0;
    render_pass_begin_info.renderArea.extent = renderer_state.swapchain_extent;
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_color;
    
    for (uint32 swapchain_image_increment = 0; swapchain_image_increment < renderer_state.swapchain_image_count; swapchain_image_increment++)
    {
        render_pass_begin_info.framebuffer = renderer_state.swapchain_framebuffers[swapchain_image_increment];

        vkBeginCommandBuffer(renderer_state.swapchain_command_buffers[swapchain_image_increment], &command_buffer_begin_info); // start recording into the CB
        vkCmdBeginRenderPass(renderer_state.swapchain_command_buffers[swapchain_image_increment], &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE); // the clear actually happens
        vkCmdEndRenderPass(renderer_state.swapchain_command_buffers[swapchain_image_increment]); // triggers the transition toward final layout declared, so image is ready to present
        vkEndCommandBuffer(renderer_state.swapchain_command_buffers[swapchain_image_increment]); // seals CB - now immutable until reset
    }

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

    renderer_state.frames_in_flight = 1;
	renderer_state.current_frame = 0;
	renderer_state.image_available_semaphores = malloc(sizeof(VkSemaphore) * renderer_state.frames_in_flight);
    renderer_state.render_finished_semaphores = malloc(sizeof(VkSemaphore) * renderer_state.frames_in_flight);
    renderer_state.in_flight_fences = malloc(sizeof(VkFence) * renderer_state.frames_in_flight);
											
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

    for (uint32 frames_in_flight_increment = 0; frames_in_flight_increment < renderer_state.frames_in_flight; frames_in_flight_increment++)
    {
        vkCreateSemaphore(renderer_state.logical_device_handle, &semaphore_info, 0, &renderer_state.image_available_semaphores[frames_in_flight_increment]);
        vkCreateSemaphore(renderer_state.logical_device_handle, &semaphore_info, 0, &renderer_state.render_finished_semaphores[frames_in_flight_increment]);
        vkCreateFence(renderer_state.logical_device_handle, &fence_info, 0, &renderer_state.in_flight_fences[frames_in_flight_increment]);

        // TODO(spike): should do some bailouts here
	}

    // used in the draw loop:

    // these might return the same handle, but that's fine
    vkGetDeviceQueue(renderer_state.logical_device_handle, renderer_state.graphics_family_index, 0, &renderer_state.graphics_queue_handle);
    vkGetDeviceQueue(renderer_state.logical_device_handle, renderer_state.present_family_index, 0, &renderer_state.present_queue_handle);


	// STAGE AND UPLOAD VERTEX / INDEX BUFFER FOR SPRITES AND CUBES
    renderer_state.sprite_index_count = 6;
	uploadBufferToLocalDevice(SPRITE_VERTICES, sizeof(SPRITE_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &renderer_state.sprite_vertex_buffer, &renderer_state.sprite_vertex_memory);
	uploadBufferToLocalDevice(SPRITE_INDICES,  sizeof(SPRITE_INDICES),  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  &renderer_state.sprite_index_buffer,  &renderer_state.sprite_index_memory);

	renderer_state.cube_index_count = 36;
	uploadBufferToLocalDevice(CUBE_VERTICES, sizeof(CUBE_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &renderer_state.cube_vertex_buffer, &renderer_state.cube_vertex_memory);
	uploadBufferToLocalDevice(CUBE_INDICES,  sizeof(CUBE_INDICES),  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  &renderer_state.cube_index_buffer,  &renderer_state.cube_index_memory);

    renderer_state.images_in_flight = calloc(renderer_state.swapchain_image_count, sizeof(VkFence)); // calloc because we want these to start at VK_NULL_HANDLE, i.e. 0.

    // a shader is a tiny program the GPU runs many times in a graphics pipeline.
    // vertex shader (VS): runs once per vertex. it reads vertex attributes (pos, color, etc.), transforms pos into clip space (the [-1, 1] box the gpu expects), and passes along any per-vertex data to the next stage.
    // resterizer (fixed-function): takes vertices and turns them into fragments (pixel candidates)
    // fragment shader (FS): runs once per fragment. it decides the final color, and can do texturing, lighting, etc.

	// for now this is a black box. but i am interested in this and want to revisit it later.

    // setting up vertex shader module

    void* vert_bytes = 0; // a pointer that will hold the vertex shader's SPIR-V bytes loaded from disk. 
    size_t vert_size = 0; // holds byte count of vertex shader
    if (!readEntireFile("data/shaders/spirv/tri.vert.spv", &vert_bytes, &vert_size))
    {
		return;
        // TODO(spike): various frees here
    }

    VkShaderModuleCreateInfo vertex_shader_module_creation_info = {0};
    vertex_shader_module_creation_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_shader_module_creation_info.codeSize = vert_size;
    vertex_shader_module_creation_info.pCode = (const uint32*)vert_bytes;

	// seting up fragment shader module

    VkResult vertex_shader_module_creation_result = vkCreateShaderModule(renderer_state.logical_device_handle, &vertex_shader_module_creation_info, 0, &renderer_state.vertex_shader_module_handle);
    if (vertex_shader_module_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): various frees here
    }
    free(vert_bytes);

    void* frag_bytes = 0;
    size_t frag_size = 0;
    if (!readEntireFile("data/shaders/spirv/tri.frag.spv", &frag_bytes, &frag_size))
    {
        return;
        // TODO(spike): various frees here
    }

    VkShaderModuleCreateInfo fragment_shader_module_creation_info = {0};
    fragment_shader_module_creation_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_shader_module_creation_info.codeSize = frag_size;
    fragment_shader_module_creation_info.pCode = (const uint32*)frag_bytes;

    VkResult fragment_shader_module_creation_result = vkCreateShaderModule(renderer_state.logical_device_handle, &fragment_shader_module_creation_info, 0, &renderer_state.fragment_shader_module_handle);
    if (fragment_shader_module_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): various frees here
    }
    free(frag_bytes);

    // define the pipeline shader stages that plug shader modules into the (currently empty) graphics pipeline
    // vertex shader stage

    VkPipelineShaderStageCreateInfo vertex_shader_stage_create_info = {0};
    vertex_shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertex_shader_stage_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_shader_stage_create_info.module = renderer_state.vertex_shader_module_handle;
    vertex_shader_stage_create_info.pName = "main";

    // fragment shader stage

    VkPipelineShaderStageCreateInfo fragment_shader_stage_create_info = {0};
    fragment_shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragment_shader_stage_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_shader_stage_create_info.module = renderer_state.fragment_shader_module_handle;
    fragment_shader_stage_create_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[2] = { vertex_shader_stage_create_info, fragment_shader_stage_create_info }; // bundle both stages for pipeline creation
	
    VkVertexInputBindingDescription vertex_binding = {0};
    vertex_binding.binding   = 0;
    vertex_binding.stride    = sizeof(Vertex);
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attributes[3] = {0};

    vertex_attributes[0].binding  = 0;
    vertex_attributes[0].location = 0;
    vertex_attributes[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes[0].offset   = offsetof(Vertex, x);

    vertex_attributes[1].binding  = 0;
    vertex_attributes[1].location = 1;
    vertex_attributes[1].format   = VK_FORMAT_R32G32_SFLOAT;
    vertex_attributes[1].offset   = offsetof(Vertex, u);

    vertex_attributes[2].binding  = 0;
    vertex_attributes[2].location = 2;
    vertex_attributes[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes[2].offset   = offsetof(Vertex, r);

    VkVertexInputBindingDescription bindings[] = { vertex_binding };

    VkPipelineVertexInputStateCreateInfo vertex_input_state_creation_info = {0};
    vertex_input_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_creation_info.vertexBindingDescriptionCount   = (uint32)(sizeof(bindings)    / sizeof(bindings[0]));
    vertex_input_state_creation_info.pVertexBindingDescriptions      = bindings;
    vertex_input_state_creation_info.vertexAttributeDescriptionCount = (uint32)(sizeof(vertex_attributes)/ sizeof(vertex_attributes[0]));
    vertex_input_state_creation_info.pVertexAttributeDescriptions    = vertex_attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_creation_info = {0}; // struct that describes how vertices are assembled into primatives before rasterization
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

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }; // we will set each frame before drawing

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
    rasterization_state_creation_info.depthBiasEnable = VK_FALSE; // no depth bias: adds a small offset to a fragment's depth before the depth test. helps with z-fighting with multiple layers, and some other stuff
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

	VkPipelineColorBlendAttachmentState color_blend_attachment_state = {0}; // controls per-render-target blending, i.e., how the fragment shader's output color is combined with what's already there. for now, just write RGBA
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
    color_blend_state_creation_info.attachmentCount = 1; // one attachment in the subpass: the swapchain image
    color_blend_state_creation_info.pAttachments = &color_blend_attachment_state;
    color_blend_state_creation_info.blendConstants[0] = 0.0f;
    color_blend_state_creation_info.blendConstants[1] = 0.0f;
    color_blend_state_creation_info.blendConstants[2] = 0.0f;
    color_blend_state_creation_info.blendConstants[3] = 0.0f;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_creation_info = {0}; // depth/stencil settings for the pipeline (not using this for simple 2D)
	depth_stencil_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil_state_creation_info.depthTestEnable =  VK_TRUE;
	depth_stencil_state_creation_info.depthWriteEnable = VK_TRUE;
	depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;
	depth_stencil_state_creation_info.depthBoundsTestEnable = VK_FALSE;
	depth_stencil_state_creation_info.stencilTestEnable = VK_FALSE;

	// descriptors + pipeline layout (shared by sprite and cube pipelines)

    VkSamplerCreateInfo sampler_creation_info = {0};
    sampler_creation_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; // TODO(spike): double check all this info at some point
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

    vkCreateSampler(renderer_state.logical_device_handle, &sampler_creation_info, 0, &renderer_state.pixel_art_sampler);

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

    vkCreateDescriptorSetLayout(renderer_state.logical_device_handle, &descriptor_set_layout_creation_info, 0, &renderer_state.descriptor_set_layout);

    // descriptor pool allocates memory for all descriptor sets
    VkDescriptorPoolSize descriptor_pool_size = {0};
    descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_pool_size.descriptorCount = 1024;
    
    VkDescriptorPoolCreateInfo descriptor_pool_creation_info = {0};
    descriptor_pool_creation_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_creation_info.poolSizeCount = 1;
    descriptor_pool_creation_info.pPoolSizes = &descriptor_pool_size;
    descriptor_pool_creation_info.maxSets = 1024;

    vkCreateDescriptorPool(renderer_state.logical_device_handle, &descriptor_pool_creation_info, 0, &renderer_state.descriptor_pool);

    VkPushConstantRange push_constant_range = {0};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset     = 0;
    push_constant_range.size       = (uint32)sizeof(PushConstants); 

    VkPipelineLayoutCreateInfo graphics_pipeline_layout_creation_info = {0};
    graphics_pipeline_layout_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    graphics_pipeline_layout_creation_info.setLayoutCount = 1;
    graphics_pipeline_layout_creation_info.pSetLayouts = &renderer_state.descriptor_set_layout; 
    graphics_pipeline_layout_creation_info.pushConstantRangeCount = 1;
    graphics_pipeline_layout_creation_info.pPushConstantRanges = &push_constant_range;

    vkCreatePipelineLayout(renderer_state.logical_device_handle, &graphics_pipeline_layout_creation_info, 0, &renderer_state.graphics_pipeline_layout);

    // base graphics pipeline info

   	VkGraphicsPipelineCreateInfo base_graphics_pipeline_creation_info = {0}; // struct that points to all those sub-blocks we just defined; it actually builds the pipeline object
	base_graphics_pipeline_creation_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	base_graphics_pipeline_creation_info.stageCount = 2;
	base_graphics_pipeline_creation_info.pStages = shader_stages;
	base_graphics_pipeline_creation_info.pVertexInputState = &vertex_input_state_creation_info;
	base_graphics_pipeline_creation_info.pInputAssemblyState = &input_assembly_state_creation_info;
	base_graphics_pipeline_creation_info.pViewportState = &viewport_state_creation_info;
	base_graphics_pipeline_creation_info.pRasterizationState = &rasterization_state_creation_info; // fill mode, cull off
	base_graphics_pipeline_creation_info.pMultisampleState = &multisample_state_creation_info; // multisampling disabled (1x MSAA)
	base_graphics_pipeline_creation_info.pDepthStencilState = &depth_stencil_state_creation_info;
	base_graphics_pipeline_creation_info.pColorBlendState = &color_blend_state_creation_info; // one color attachment; no blending
	base_graphics_pipeline_creation_info.pDynamicState = &dynamic_state_creation_info; // declares that viewport / scissor are dynamic
	base_graphics_pipeline_creation_info.layout = renderer_state.graphics_pipeline_layout;
	base_graphics_pipeline_creation_info.renderPass = renderer_state.render_pass_handle;
	base_graphics_pipeline_creation_info.subpass = 0; // first (and only) subpass
	base_graphics_pipeline_creation_info.basePipelineHandle = VK_NULL_HANDLE; // not deriving from another pipeline.
	base_graphics_pipeline_creation_info.basePipelineIndex = -1;

    renderer_state.atlas_2d_asset_index   = getOrLoadAsset((char*)ATLAS_2D_PATH);
    renderer_state.atlas_font_asset_index = getOrLoadAsset((char*)ATLAS_FONT_PATH);
    renderer_state.atlas_3d_asset_index   = getOrLoadAsset((char*)ATLAS_3D_PATH);

    // sprite pipeline: depth off, blending on
    {
        color_blend_attachment_state.blendEnable = VK_TRUE;
        color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

        depth_stencil_state_creation_info.depthTestEnable = VK_FALSE;
		depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkGraphicsPipelineCreateInfo sprite_ci = base_graphics_pipeline_creation_info;
        vkCreateGraphicsPipelines(renderer_state.logical_device_handle, VK_NULL_HANDLE, 1, &sprite_ci, 0, &renderer_state.sprite_pipeline_handle);
    }

	// cube pipeline: depth on, blending off
    {
        color_blend_attachment_state.blendEnable = VK_FALSE;
        
        depth_stencil_state_creation_info.depthTestEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthWriteEnable = VK_TRUE;
        depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo cube_ci = base_graphics_pipeline_creation_info;
        vkCreateGraphicsPipelines(renderer_state.logical_device_handle, VK_NULL_HANDLE, 1, &cube_ci, 0, &renderer_state.cube_pipeline_handle);
    }
}

void rendererSubmitFrame(AssetToLoad assets_to_load[1024], Camera game_camera)
{  
	renderer_camera = game_camera;
    sprite_instance_count = 0;
	cube_instance_count = 0;

    for (int asset_index = 0; asset_index < 1024; asset_index++)
    {
		AssetToLoad* batch = &assets_to_load[asset_index];
        if (batch->instance_count == 0) continue;

        AssetType type = batch->type;
        SpriteId sprite_id = batch->sprite_id;

		int32 atlas_asset_index = -1;
        int32 atlas_width = 0;
        int32 atlas_height = 0;

        if (type == SPRITE_2D)
        {
            if (spriteIsFont(sprite_id))
            {
                atlas_asset_index = renderer_state.atlas_font_asset_index;
                atlas_width = ATLAS_FONT_WIDTH;
                atlas_height = ATLAS_FONT_HEIGHT;
            }
            else
            {
                atlas_asset_index = renderer_state.atlas_2d_asset_index;
                atlas_width = ATLAS_2D_WIDTH;
                atlas_height = ATLAS_2D_HEIGHT;
            }
        }
        else if (type == CUBE_3D)
        {
            atlas_asset_index = renderer_state.atlas_3d_asset_index;
            atlas_width = ATLAS_3D_WIDTH;
            atlas_height = ATLAS_3D_HEIGHT;
        }

        if (atlas_asset_index < 0) continue;

        Vec4 uv_rect = spriteUV(sprite_id, type, atlas_width, atlas_height);

        if (assets_to_load[asset_index].type == SPRITE_2D)
        {
			for (int32 sprite_instance_index = 0; sprite_instance_index < assets_to_load[asset_index].instance_count; sprite_instance_index++)
            {
                Sprite* sprite = &sprite_instances[sprite_instance_count++];
                sprite->asset_index = (uint32)atlas_asset_index;
                sprite->coords      = batch->coords[sprite_instance_index];
                sprite->size        = batch->scale[sprite_instance_index];
                sprite->uv          = uv_rect;
            }
        }
        else if (assets_to_load[asset_index].type == CUBE_3D)
        {
            for (int32 cube_instance_index = 0; cube_instance_index < assets_to_load[asset_index].instance_count; cube_instance_index++)
            {
                Cube* cube= &cube_instances[cube_instance_count++];
                cube->asset_index = (uint32)atlas_asset_index;
                cube->coords      = batch->coords[cube_instance_index];
                cube->scale       = batch->scale[cube_instance_index];
                cube->rotation    = batch->rotation[cube_instance_index];
                cube->uv          = uv_rect;
            }
        }
    }
}

void rendererDraw(void)
{
    // THROTTLE TO N FRAMES IN FLIGHT

    // blocks until the previous GPU submission that used this slot has finised. if GPU is still using that slot, CPU must wait (i.e. you cannot get more than N frames ahead)
	vkWaitForFences(renderer_state.logical_device_handle, 1, &renderer_state.in_flight_fences[renderer_state.current_frame], VK_TRUE, UINT64_MAX);

    // ACQUIRE A SWAPCHAIN IMAGE

	uint32 swapchain_image_index = 0;
    // picks which swapchain image you may render to next, and tells the GPU when the image is ready via a semaphore
    VkResult acquire_result = vkAcquireNextImageKHR(renderer_state.logical_device_handle, renderer_state.swapchain_handle, UINT64_MAX, renderer_state.image_available_semaphores[renderer_state.current_frame], VK_NULL_HANDLE, &swapchain_image_index);

	switch (acquire_result)
    {
        case VK_SUCCESS:
            break;
        case VK_SUBOPTIMAL_KHR:
            break;
		case VK_ERROR_OUT_OF_DATE_KHR:
            return; // trigger swapchain recreate before drawing
    	case VK_ERROR_SURFACE_LOST_KHR:
            return; // recreate surface (then swapchain)
        default:
            return;
    }

    // PER-IMAGE FENCE BOOK-KEEPING

	if (renderer_state.images_in_flight[swapchain_image_index] != VK_NULL_HANDLE)
    {
        vkWaitForFences(renderer_state.logical_device_handle, 1, &renderer_state.images_in_flight[swapchain_image_index], VK_TRUE, UINT64_MAX);
    }

    renderer_state.images_in_flight[swapchain_image_index] = renderer_state.in_flight_fences[renderer_state.current_frame]; // record that this frame-slot's fence now owns this image.
                                                                                                                            // next time this image is acquired, we'll wait on this fence if needed
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // GPU needs to know what pipeline stage is blocked until the semaphore signals.

    vkResetFences(renderer_state.logical_device_handle, 1, &renderer_state.in_flight_fences[renderer_state.current_frame]);

    // actual things happening

    VkCommandBuffer command_buffer = renderer_state.swapchain_command_buffers[swapchain_image_index]; // select command buffer that corresponds to the acquired swapchain image.
    vkResetCommandBuffer(command_buffer, 0); // throw away last frame's commands for this image and start fresh.

	VkCommandBufferBeginInfo command_buffer_begin_info = {0};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // submit once, reset next frame
	vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    VkClearValue clear_values[2];
    clear_values[0].color.float32[0] = 0.005f;
    clear_values[0].color.float32[1] = 0.008f;
    clear_values[0].color.float32[2] = 0.02f;
    clear_values[0].color.float32[3] = 1.0f;

    clear_values[1].depthStencil.depth = 1.0f;
    clear_values[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo render_pass_begin_info = {0};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = renderer_state.render_pass_handle;
    render_pass_begin_info.framebuffer = renderer_state.swapchain_framebuffers[swapchain_image_index];
    render_pass_begin_info.renderArea.offset = (VkOffset2D){ 0,0 };
    render_pass_begin_info.renderArea.extent = renderer_state.swapchain_extent;
    render_pass_begin_info.clearValueCount = 2;
    render_pass_begin_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    // dynamic pipeline state (same baked graphics pipeline, but change viewport / scissor whenever we change what's in frame. for now we don't really use this though)

    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = (float)renderer_state.swapchain_extent.height;
    viewport.width = (float)renderer_state.swapchain_extent.width;
    viewport.height = -(float)renderer_state.swapchain_extent.height; // negative for y-up
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
	vkCmdSetViewport(command_buffer, 0, 1, &viewport); // TODO(spike): look at this more
	VkRect2D scissor = {0};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = renderer_state.swapchain_extent;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

	// CUBE PIPELINE

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer_state.cube_pipeline_handle);

    VkDeviceSize cube_vb_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &renderer_state.cube_vertex_buffer, &cube_vb_offset);
    vkCmdBindIndexBuffer(command_buffer, renderer_state.cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);

    if (renderer_state.asset_cache_count > 0)
    {
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer_state.graphics_pipeline_layout, 0, 1, &renderer_state.descriptor_sets[0], 0, 0);
    }

	// cube logics

    float aspect = (float)renderer_state.swapchain_extent.width / (float)renderer_state.swapchain_extent.height;
	float projection_matrix[16], view_matrix[16];
    mat4BuildPerspective(projection_matrix, renderer_camera.fov * (6.2831831f/360.0f), aspect, 0.1f, 100.0f);
	mat4BuildViewFromQuat(view_matrix, renderer_camera.coords, renderer_camera.rotation);

    int32 last_cube_asset = -1;
    for (uint32 cube_instance_index = 0; cube_instance_index < cube_instance_count; cube_instance_index++)
    {
        Cube* cube= &cube_instances[cube_instance_index];

        if ((int32)cube->asset_index != last_cube_asset)
        {
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer_state.graphics_pipeline_layout, 0, 1, &renderer_state.descriptor_sets[cube->asset_index], 0, 0);
            last_cube_asset = (int32)cube->asset_index;
        }

        float model_matrix[16];
        mat4BuildTRS(model_matrix, cube->coords, cube->rotation, cube->scale);
		
        PushConstants push_constants = {0};
        memcpy(push_constants.model, model_matrix, 		sizeof(push_constants.model));
        memcpy(push_constants.view,  view_matrix, 		sizeof(push_constants.view));
        memcpy(push_constants.proj,  projection_matrix, sizeof(push_constants.proj));
        push_constants.uv_rect = cube->uv;

        vkCmdPushConstants(command_buffer, renderer_state.graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
        
        vkCmdDrawIndexed(command_buffer, renderer_state.cube_index_count, 1, 0, 0, 0);
    }

	// SPRITE PIPELINE

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer_state.sprite_pipeline_handle); 

	VkDeviceSize sprite_vb_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &renderer_state.sprite_vertex_buffer, &sprite_vb_offset);
    vkCmdBindIndexBuffer(command_buffer, renderer_state.sprite_index_buffer, 0, VK_INDEX_TYPE_UINT32);

    float ortho[16], view2d[16];
    mat4BuildOrtho(ortho,
            0.0f, (float)renderer_state.swapchain_extent.width,
            0.0f, (float)renderer_state.swapchain_extent.height,
            0.0f, 1.0f);
    mat4Identity(view2d);

    int32 last_sprite_asset = -1;

	for (uint32 sprite_instance_index = 0; sprite_instance_index < sprite_instance_count; sprite_instance_index++)
    {
        Sprite* sprite = &sprite_instances[sprite_instance_index];

        if ((int32)sprite->asset_index != last_sprite_asset)
        {
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer_state.graphics_pipeline_layout, 0, 1, &renderer_state.descriptor_sets[sprite->asset_index], 0, 0);
            last_sprite_asset = (int32)sprite->asset_index;
        }

        float model_matrix[16];
        Vec4 identity_quaternion = { 0, 0, 0, 1}; // TODO(spike): make global
        mat4BuildTRS(model_matrix, sprite->coords, identity_quaternion, sprite->size);

        PushConstants push_constants = {0};
        memcpy(push_constants.model, model_matrix, sizeof(push_constants.model));
        memcpy(push_constants.view,  view2d, 	   sizeof(push_constants.view));
        memcpy(push_constants.proj,  ortho, 	   sizeof(push_constants.proj));
        push_constants.uv_rect = sprite->uv;

        vkCmdPushConstants(command_buffer, renderer_state.graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);

        vkCmdDrawIndexed(command_buffer, renderer_state.sprite_index_count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(command_buffer);
    vkEndCommandBuffer(command_buffer);

	first_submit_since_draw = true;

    // SUBMIT THE PRE-RECORDED CB FOR THAT IMAGE

    VkSubmitInfo submit_info = {0}; // container for the GPU submission: which semaphores to wait on, which command buffer to execute, and which semaphore to signal when done.
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &renderer_state.image_available_semaphores[renderer_state.current_frame];
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &renderer_state.swapchain_command_buffers[swapchain_image_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &renderer_state.render_finished_semaphores[renderer_state.current_frame];

    VkResult submit_result = vkQueueSubmit(renderer_state.graphics_queue_handle, 1, &submit_info, renderer_state.in_flight_fences[renderer_state.current_frame]);
    
    if (submit_result != VK_SUCCESS) 
    {
        // fallback: signal the fence with an empty submit so the next frame won't hang
        VkSubmitInfo empty_submit = {0};
        empty_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(renderer_state.graphics_queue_handle, 1, &empty_submit, renderer_state.in_flight_fences[renderer_state.current_frame]);
    }

    // PRESENT

    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &renderer_state.render_finished_semaphores[renderer_state.current_frame];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &renderer_state.swapchain_handle;
    present_info.pImageIndices = &swapchain_image_index;

    VkResult present_result = vkQueuePresentKHR(renderer_state.present_queue_handle, &present_info);

    switch (present_result)
	{
        case VK_SUCCESS:
            break;
        case VK_SUBOPTIMAL_KHR:
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
            return; // swapchain no longer matches the surface; recreate before drawing again
        case VK_ERROR_SURFACE_LOST_KHR:
            return; // surface died; recreate surface (then swapchain)
        default:
        	return;
    }

    // ADVANCE TO NEXT FRAME SLOT

	renderer_state.current_frame = (renderer_state.current_frame + 1) % renderer_state.frames_in_flight;
}

void rendererResize(uint32 width, uint32 height)
{
    (void)width; // TODO(spike): get rid of these when we actually use these variables
    (void)height;
}

void rendererShutdown(void)
{

}
