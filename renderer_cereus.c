#define VK_USE_PLATFORM_WIN32_KHR

#include <stdio.h>
#include "win32_renderer_bridge.h"
#include <vulkan/vulkan.h>

internal Vertex frame_vertex_stash[1024];
internal uint32 frame_vertex_count = 0;

typedef struct
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

    VkBuffer vertex_buffer_handle;
    VkDeviceMemory vertex_memory;
    void* mapped_vertex_pointer;
    VkShaderModule vertex_shader_module_handle;
    VkShaderModule fragment_shader_module_handle;
    VkPipelineLayout graphics_pipeline_layout; 
    VkPipeline graphics_pipeline_handle;
}
RendererState;
RendererState renderer_state;

internal uint32 findMemoryType(uint32 type_bits, VkMemoryPropertyFlags property_flags)
{
    VkPhysicalDeviceMemoryProperties memory_properties = {0};
    vkGetPhysicalDeviceMemoryProperties(renderer_state.physical_device_handle, &memory_properties); // TODO(spike): could remove dependency on renderer_state here by passing it, but probably not an issue (read only)
    for (uint32 memory_type_count_increment = 0; memory_properties.memoryTypeCount; memory_type_count_increment++)
    {
        bool is_compatible = (type_bits & (1u << memory_type_count_increment)) != 0;
        bool has_properties = (memory_properties.memoryTypes[memory_type_count_increment].propertyFlags & property_flags) == property_flags;
        if (is_compatible && has_properties) return 1;
    }
    return UINT32_MAX;
}

internal bool readEntireFile(char* path, void** out_data, size_t* out_size) // TODO(spike): temporary. there's a malloc here, and should probably put size_t in types.h. moreover, shouldn't this happen at the platform layer?
{
	FILE* file = fopen(path, "rb"); // read binary
    if (!file) return false;
    fseek(file, 0, SEEK_END); // move stream's file position to 0 bytes from the end (i.e. just past the last byte)
   	long end_position = ftell(file); // return current file position, i.e. size of the file in bytes
    fseek(file, 0, SEEK_SET); // seeks back to the start, so the file can be read from byte 0
	size_t file_size_bytes = (size_t)end_position;
	void* file_bytes = malloc(file_size_bytes); // TODO(spike): bump-alloc here, after set up big alloction on platform layer
	if (!file_bytes) {
        fclose(file);
        return false;
    }
    size_t bytes_read = fread(file_bytes, 1, file_size_bytes, file); // TODO(spike): another size_t here
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

    VkResult instance_creation_result = vkCreateInstance(&instance_creation_info, 0, &renderer_state.vulkan_instance_handle);

    if (instance_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): instance needs cleanup
    }

    // struct that holds info that the surfaces uses to talk to platform layer
	VkWin32SurfaceCreateInfoKHR surface_creation_info = {0};
    surface_creation_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_creation_info.hinstance = renderer_state.platform_handles.module_handle;
    surface_creation_info.hwnd = renderer_state.platform_handles.window_handle;

	VkResult surface_creation_result = vkCreateWin32SurfaceKHR(renderer_state.vulkan_instance_handle, &surface_creation_info, 0, &renderer_state.surface_handle);

    if (surface_creation_result != VK_SUCCESS) 
    {
        return;
        // TODO(spike): instance needs cleanup
    }

	uint32 device_count = 0;
    vkEnumeratePhysicalDevices(renderer_state.vulkan_instance_handle, &device_count, 0);

	if (device_count == 0)
    {
        return;
        // TODO(spike): surface + instance need cleanup
    }

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
    
    if (present_mode_count == 0)
    {
        return;
        // TODO(spike): surface + instance needs cleanup
    }

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

	if (surface_format_count == 0)
    {
        return;
        // TODO(spike): surface + instance need cleanup
    }

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

    if (graphics_present_families_same)
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

    VkResult logical_device_creation_result = vkCreateDevice(renderer_state.physical_device_handle, &device_info, 0, &renderer_state.logical_device_handle);

    if (logical_device_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): instance + surface needs cleanup
    }

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

    if (graphics_present_families_same == false) 
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

	VkResult swapchain_creation_result = vkCreateSwapchainKHR(renderer_state.logical_device_handle, &swapchain_creation_info, 0, &renderer_state.swapchain_handle);

    if (swapchain_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): logical device + surface + instance need cleanup
    }

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
        VkResult view_creation_result = vkCreateImageView(renderer_state.logical_device_handle, &view_creation_info, 0, &renderer_state.swapchain_image_views[swapchain_image_increment]);
        if (view_creation_result != VK_SUCCESS)
        {
            free(swapchain_images);
            free(renderer_state.swapchain_image_views);
            return;
            // TODO(spike): swapchain + logical device + surface + instance need cleanup
        }
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
	color_attachment_reference.attachment = 0; // to be explicit about that we are getting the first (only) attachment
    color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // write color layout in optimal layout for color output
	
    VkSubpassDescription color_output_subpass = {0}; // only one subpass per frame for our minimal setup
	color_output_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    color_output_subpass.colorAttachmentCount = 1;
    color_output_subpass.pColorAttachments = &color_attachment_reference;
    
    VkSubpassDependency color_output_subpass_dependency = {0}; // encodes memory + exectution ordering between stages outside the render pass and stages inside the subpass
	color_output_subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // declares source as 'outside the render pass' - there's only one subpass so the source has to be external here
	color_output_subpass_dependency.dstSubpass = 0; // set first (and only) subpass as destination of the dependency
	color_output_subpass_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // some ordering shenanigans
    color_output_subpass_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	color_output_subpass_dependency.srcAccessMask = 0; // we don't rely on any prior contents
	color_output_subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // we want to protect the attachment at the destination
	
    uint32 attachment_count = 1; // how many attachment descriptions exist in the whole render pass (color, depth/stencil...) 

    VkRenderPassCreateInfo render_pass_creation_info = {0}; // container that ties attachment(s), subpass(es), and dependency(ies) into a single render pass object.
	render_pass_creation_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_creation_info.attachmentCount = attachment_count; 
    render_pass_creation_info.pAttachments = &color_attachment;
    render_pass_creation_info.subpassCount = 1;
	render_pass_creation_info.pSubpasses = &color_output_subpass; // only one subpass, so just pass a pointer to where that subpass is, rather than some array of subpasses.
    render_pass_creation_info.dependencyCount = 1;
    render_pass_creation_info.pDependencies = &color_output_subpass_dependency; // same story here - just a pointer to our one dependency, rather than an array.
	
    VkResult render_pass_creation_result = vkCreateRenderPass(renderer_state.logical_device_handle, &render_pass_creation_info, 0, &renderer_state.render_pass_handle);

    if (render_pass_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): images + swapchain + device + surface + instance need cleanup
    }

	// a framebuffer is the binding of the render pass' attachment slots to specific image views, with a fixed size (width/height) and layer count. 
    // it doesn't allocate memory, it just ties the render pass to the actual image 
    // one framebuffer per swapchan image view, because each acquired image is a different underlying image view

    renderer_state.swapchain_framebuffers = malloc(sizeof(VkFramebuffer) * renderer_state.swapchain_image_count);

    VkFramebufferCreateInfo framebuffer_creation_info = {0};
    framebuffer_creation_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_creation_info.renderPass = renderer_state.render_pass_handle;
    framebuffer_creation_info.attachmentCount = attachment_count;
    framebuffer_creation_info.width = renderer_state.swapchain_extent.width;
    framebuffer_creation_info.height = renderer_state.swapchain_extent.height;
    framebuffer_creation_info.layers = 1;

    for (uint32 swapchain_image_increment = 0; swapchain_image_increment < renderer_state.swapchain_image_count; swapchain_image_increment++)
    {
		framebuffer_creation_info.pAttachments = &renderer_state.swapchain_image_views[swapchain_image_increment];
        VkResult framebuffer_creation_result = vkCreateFramebuffer(renderer_state.logical_device_handle, &framebuffer_creation_info, 0, &renderer_state.swapchain_framebuffers[swapchain_image_increment]);

        if (framebuffer_creation_result != VK_SUCCESS)
        {
            return;
            // TODO(spike): render pass + images + swapchain + device + surface + instance need cleanup
        }
    }

	VkCommandPoolCreateInfo command_pool_creation_info = {0}; // describes the command pool tied to graphics queue family
	command_pool_creation_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_creation_info.queueFamilyIndex = renderer_state.graphics_family_index;
    command_pool_creation_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // lets us reset / rerecord command buffers

	VkResult command_pool_creation_result = vkCreateCommandPool(renderer_state.logical_device_handle, &command_pool_creation_info, 0, &renderer_state.graphics_command_pool_handle);

    if (command_pool_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): framebuffer + render pass + images + swapchain + device + surface + instance need cleanup
    }

    renderer_state.swapchain_command_buffers = malloc(sizeof(VkCommandBuffer) * renderer_state.swapchain_image_count);

	VkCommandBufferAllocateInfo command_buffer_allocation_info = {0}; // which command pool (memory) to allocate from; how many command buffers to allocate in one shot
    command_buffer_allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocation_info.commandPool = renderer_state.graphics_command_pool_handle;
    command_buffer_allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // primary command buffers submit directly to the graphics queue. secondary CBs are for nesting / multithreading 
    command_buffer_allocation_info.commandBufferCount = renderer_state.swapchain_image_count;
	
    VkResult command_buffer_allocation_result = vkAllocateCommandBuffers(renderer_state.logical_device_handle, &command_buffer_allocation_info, renderer_state.swapchain_command_buffers);

	if (command_buffer_allocation_result != VK_SUCCESS)
    {
		return;
        // TODO(spike): command pool + framebuffer + render pass + images + swapchain + device + surface + instance need cleanup
    }

	VkCommandBufferBeginInfo command_buffer_begin_info = {0}; // container for how a command buffer begins recording (unused for us, no flags needed)
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkClearValue clear_color = { .color = { .float32 = { 0.02f, 0.2f, 0.03f, 1.0f } } };
    
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

    renderer_state.images_in_flight = calloc(renderer_state.swapchain_image_count, sizeof(VkFence)); // calloc because we want these to start at VK_NULL_HANDLE, i.e. 0.

	// triangle time (should all be mostly temporary) TODO(spike): re-evaluate this after i've set up a fixed-size memory block for the entire game
    VkDeviceSize dynamic_vertex_stream_bytes = 1024 * sizeof(Vertex);

    VkBufferCreateInfo buffer_creation_info = {0};
    buffer_creation_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_creation_info.size = dynamic_vertex_stream_bytes;
    buffer_creation_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_creation_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // should probably test on excl. vs. concurrent here, depending on what we did before?

    vkCreateBuffer(renderer_state.logical_device_handle, &buffer_creation_info, 0, &renderer_state.vertex_buffer_handle);

    VkMemoryRequirements memory_requirements = {0};
    vkGetBufferMemoryRequirements(renderer_state.logical_device_handle, renderer_state.vertex_buffer_handle, &memory_requirements);
	uint32 memory_type_index = findMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (memory_type_index == UINT32_MAX)
    {
        return;
    }

    VkMemoryAllocateInfo memory_allocation_info = {0};
    memory_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    memory_allocation_info.allocationSize = memory_requirements.size;
    memory_allocation_info.memoryTypeIndex = memory_type_index;

    vkAllocateMemory(renderer_state.logical_device_handle, &memory_allocation_info, 0, &renderer_state.vertex_memory);

    vkBindBufferMemory(renderer_state.logical_device_handle, renderer_state.vertex_buffer_handle, renderer_state.vertex_memory, 0);
	vkMapMemory(renderer_state.logical_device_handle, renderer_state.vertex_memory, 0, VK_WHOLE_SIZE, 0, &renderer_state.mapped_vertex_pointer);

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
	
    VkVertexInputBindingDescription vertex_binding = {0}; // describes how the Vertex type is laid out in the vertex bufer (stride, rate)
    vertex_binding.binding = 0;
    vertex_binding.stride = sizeof(Vertex); // tightly packed
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertex_attribute_pos = {0}; // describes the pos attribute of a Vertex; where it comes from, which shader location it feeds, some other stuff
    vertex_attribute_pos.location = 0;
    vertex_attribute_pos.binding = 0;
    vertex_attribute_pos.format = VK_FORMAT_R32G32_SFLOAT; // two 32-bit floats, matching vec2 position
	vertex_attribute_pos.offset = offsetof(Vertex, pos);

    VkVertexInputAttributeDescription vertex_attribute_color = {0};
    vertex_attribute_color.location = 1;
    vertex_attribute_color.binding = 0;
    vertex_attribute_color.format = VK_FORMAT_R32G32B32_SFLOAT; // three 32-bit floats, matching vec3 color
	vertex_attribute_color.offset = offsetof(Vertex, color);

	VkVertexInputBindingDescription bindings[] = { vertex_binding };
    VkVertexInputAttributeDescription attributes[] = { vertex_attribute_pos, vertex_attribute_color };

    VkPipelineVertexInputStateCreateInfo vertex_input_state_creation_info = {0}; // struct that describes how vertex bindings/attributes feed the graphics pipeline
	vertex_input_state_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state_creation_info.vertexBindingDescriptionCount = (uint32)(sizeof(bindings) / sizeof(bindings[0]));
	vertex_input_state_creation_info.pVertexBindingDescriptions = bindings;
	vertex_input_state_creation_info.vertexAttributeDescriptionCount = (uint32)(sizeof(attributes) / sizeof(attributes[0]));
	vertex_input_state_creation_info.pVertexAttributeDescriptions = attributes;

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

	VkPipelineColorBlendAttachmentState color_blend_attachment_state = {0}; // controls per-render-target blending, i.e., how the fragment shader's output color is combined with what's already there. for now, just write RGBA
    color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment_state.blendEnable = VK_FALSE;

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
	depth_stencil_state_creation_info.depthTestEnable = VK_FALSE;
	depth_stencil_state_creation_info.depthWriteEnable = VK_FALSE;
	depth_stencil_state_creation_info.depthCompareOp = VK_COMPARE_OP_ALWAYS;
	depth_stencil_state_creation_info.depthBoundsTestEnable = VK_FALSE;
	depth_stencil_state_creation_info.minDepthBounds = 0.0f;
	depth_stencil_state_creation_info.maxDepthBounds = 1.0f;
	depth_stencil_state_creation_info.stencilTestEnable = VK_FALSE;
	depth_stencil_state_creation_info.front = (VkStencilOpState){0};
	depth_stencil_state_creation_info.back = (VkStencilOpState){0};

    // a graphics pipeline is a big bundle of stuff the GPU needs to turn vertices into pixels for a specific render pass.
    // contains great things like: shaders: which vertex/fragment programs to run; input assembly: how to interpret vertices (e.g. this is a triangle); rasterisation rules, color blend...

    VkPipelineLayoutCreateInfo pipeline_layout_creation_info = {0}; // create empty pipeline for now
    pipeline_layout_creation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

	vkCreatePipelineLayout(renderer_state.logical_device_handle, &pipeline_layout_creation_info, 0, &renderer_state.graphics_pipeline_layout);

   	VkGraphicsPipelineCreateInfo graphics_pipeline_creation_info = {0}; // struct that points to all those sub-blocks we just defined; it actually builds the pipeline object
	graphics_pipeline_creation_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphics_pipeline_creation_info.stageCount = 2;
	graphics_pipeline_creation_info.pStages = shader_stages;
	graphics_pipeline_creation_info.pVertexInputState = &vertex_input_state_creation_info;
	graphics_pipeline_creation_info.pInputAssemblyState = &input_assembly_state_creation_info;
	graphics_pipeline_creation_info.pViewportState = &viewport_state_creation_info;
	graphics_pipeline_creation_info.pRasterizationState = &rasterization_state_creation_info; // fill mode, cull off
	graphics_pipeline_creation_info.pMultisampleState = &multisample_state_creation_info; // multisampling disabled (1x MSAA)
	graphics_pipeline_creation_info.pDepthStencilState = &depth_stencil_state_creation_info; // depth / stencil disabled
	graphics_pipeline_creation_info.pColorBlendState = &color_blend_state_creation_info; // one color attachment; no blending
	graphics_pipeline_creation_info.pDynamicState = &dynamic_state_creation_info; // declares that viewport / scissor are dynamic
	graphics_pipeline_creation_info.layout = renderer_state.graphics_pipeline_layout;
	graphics_pipeline_creation_info.renderPass = renderer_state.render_pass_handle;
	graphics_pipeline_creation_info.subpass = 0; // first (and only) subpass
	graphics_pipeline_creation_info.basePipelineHandle = VK_NULL_HANDLE; // not deriving from another pipeline.
	graphics_pipeline_creation_info.basePipelineIndex = -1;

	VkResult graphics_pipeline_creation_result = vkCreateGraphicsPipelines(renderer_state.logical_device_handle, VK_NULL_HANDLE, 1, &graphics_pipeline_creation_info, 0, &renderer_state.graphics_pipeline_handle);
    if (graphics_pipeline_creation_result != VK_SUCCESS)
    {
        return;
        // TODO(spike): various frees here
    }
}

void rendererSubmitFrame(WorldState* previous_world_state, WorldState* current_world_state, double interpolation_fraction)
{
	(void)previous_world_state; // not using these yet
    (void)interpolation_fraction;
    Vertex vertices[3] = {current_world_state->triangle.point1, current_world_state->triangle.point2, current_world_state->triangle.point3}; // variable for readability
    uint32 vertex_num = 3;
    // if (vertex_num > 1024) vertex_num = 1024;
    memcpy(frame_vertex_stash, vertices, vertex_num * sizeof(Vertex));
    frame_vertex_count = vertex_num;
}

void rendererDraw(void)
{
    // THROTTLE TO N FRAMES-IN FLIGHT

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

	// TRIANGLE TIME

    if (frame_vertex_count)
    {
		memcpy(renderer_state.mapped_vertex_pointer, frame_vertex_stash, frame_vertex_count * sizeof(Vertex)); // copies CPU vertex stash into the persistently mapped vertex buffer
    }
    VkCommandBuffer command_buffer = renderer_state.swapchain_command_buffers[swapchain_image_index]; // select command buffer that corresponds to the acquired swapchain image.
    vkResetCommandBuffer(command_buffer, 0); // throw away last frame's commands for this image and start fresh.

	VkCommandBufferBeginInfo command_buffer_begin_info = {0};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // submit once, reset next frame
	vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    VkClearValue clear_color = { .color = {{ 0.1f, 0.1f, 0.1f, 1.0f }} };

    VkRenderPassBeginInfo render_pass_begin_info = {0};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = renderer_state.render_pass_handle;
    render_pass_begin_info.framebuffer = renderer_state.swapchain_framebuffers[swapchain_image_index];
    render_pass_begin_info.renderArea.offset = (VkOffset2D){ 0,0 };
    render_pass_begin_info.renderArea.extent = renderer_state.swapchain_extent;
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    // dynamic pipeline state (same baked graphics pipeline, but change viewport / scissor whenever we change what's in frame. for now we don't really use this though)

    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)renderer_state.swapchain_extent.height,
        .width = (float)renderer_state.swapchain_extent.width,
        .height = -(float)renderer_state.swapchain_extent.height, // negative for y-up
		.minDepth = 0.0f,
        .maxDepth = 1.0f
    };
	vkCmdSetViewport(command_buffer, 0, 1, &viewport); // TODO(spike): look at this more
	VkRect2D scissor =
    {
        .offset = { 0,0 },
        .extent = renderer_state.swapchain_extent
    };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor); // TODO(spike): look at this more

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer_state.graphics_pipeline_handle); // selects which baked pipeline the GPU will use for subsequent draw calls on this command buffer

    VkDeviceSize vertex_buffer_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &renderer_state.vertex_buffer_handle, &vertex_buffer_offset); // for binding 0, use this VkBuffer, starting at offset 0.

	if (command_buffer != renderer_state.swapchain_command_buffers[swapchain_image_index]) { return; }
    if (command_buffer == VK_NULL_HANDLE) { return; }
    if (renderer_state.graphics_pipeline_handle == VK_NULL_HANDLE) { return; }
    if (renderer_state.vertex_buffer_handle     == VK_NULL_HANDLE) { return; }
    if (renderer_state.render_pass_handle       == VK_NULL_HANDLE) { return; }
    if (renderer_state.swapchain_framebuffers[swapchain_image_index] == VK_NULL_HANDLE) { return; }

    vkCmdDraw(command_buffer, frame_vertex_count, 1, 0, 0); // draws frame_vertex_count vertices, 1 instance, starting at vertex 0.

    vkCmdEndRenderPass(command_buffer);
    vkEndCommandBuffer(command_buffer);

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
