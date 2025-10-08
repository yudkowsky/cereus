#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"

#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> // TODO(spike): also temporary, for sin/cos
// #include <stdio.h> // TODO(spike): "temporary", for fopen 
// #include <windows.h> // TODO(spike): ok, actually temporary this time, for outputdebugstring

#define local_persist static
#define global_variable static
#define internal static

double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0.0;

float TAU = 6.28318530f;

Camera camera = { {0, 0, 4}, {0, 0, 0, 1} };
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;
float SENSITIVITY = 0.005f;
float MOVE_STEP = 0.05f;
Vec3 DEFAULT_SCALE = { 1.0f, 1.0f, 1.0f };

char* loaded_texture_paths[256] = {0};

AssetToLoad assets_to_load[256] = {0};
int32 assent_to_load_count = 0;

Entity boxes[256] = {0};
char* box_path = "data/sprites/box.png";

void cameraBasisFromYaw(float yaw, Vec3* right, Vec3* forward)
{
    float sine_yaw = sinf(yaw), cosine_yaw = cosf(yaw);
    *right   = (Vec3){ cosine_yaw, 0,   -sine_yaw };
    *forward = (Vec3){ -sine_yaw,  0, -cosine_yaw };
}

Vec4 quaternionFromAxisAngle(Vec3 axis, float angle)
{
    float sine = sinf(angle*0.5f), cosine = cosf(angle*0.5f);
	return (Vec4){ axis.x*sine, axis.y*sine, axis.z*sine, cosine};
}

Vec4 quaternionScalarMultiply(Vec4 quaternion, float scalar)
{
    return (Vec4){ quaternion.x*scalar, quaternion.y*scalar, quaternion.z*scalar, quaternion.w*scalar };
}

Vec4 quaternionMultiply(Vec4 a, Vec4 b)
{
    return (Vec4){ a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        		   a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        		   a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        		   a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}

Vec4 quaternionNormalize(Vec4 quaternion)
{
	float length_squared = quaternion.x*quaternion.x + quaternion.y*quaternion.y + quaternion.z*quaternion.z + quaternion.w*quaternion.w;
    if (length_squared <= 1e-8f) return (Vec4){0, 0, 0, 1}; 
    float inverse_length = 1.0f / sqrtf(length_squared);
    return quaternionScalarMultiply(quaternion, inverse_length);
}

Vec3 vec3CrossProduct(Vec3 a, Vec3 b)
{
	return (Vec3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

Vec3 vec3RotateByQuaternion(Vec3 input_vector, Vec4 quaternion)
{
    Vec3 quaternion_vector_part = (Vec3){ quaternion.x, quaternion.y, quaternion.z };
    float quaternion_scalar_part = quaternion.w;
	Vec3 q_cross_v = vec3CrossProduct(quaternion_vector_part, input_vector);
	Vec3 temp_vector = (Vec3){ q_cross_v.x + quaternion_scalar_part * input_vector.x,
    						   q_cross_v.y + quaternion_scalar_part * input_vector.y,
    						   q_cross_v.z + quaternion_scalar_part * input_vector.z};
    Vec3 q_cross_t = vec3CrossProduct(quaternion_vector_part, temp_vector);
    return (Vec3){ input_vector.x + 2.0f * q_cross_t.x,
    			   input_vector.y + 2.0f * q_cross_t.y,
    			   input_vector.z + 2.0f * q_cross_t.z};
}

Vec3 intCoordsToNorm(Int3 int_coords)
{
    return (Vec3){ (float)int_coords.x, (float)int_coords.y, (float)int_coords.z };
}

Vec4 directionToQuaternion(Direction direction)
{
    float yaw = 0.0f;
    switch (direction)
    {
        case NORTH: yaw = 0.0f; 		break;
        case WEST:  yaw = 0.25f  * TAU; break;
        case SOUTH: yaw = -0.1f * TAU; break;
//        case SOUTH: yaw = -0.25f * TAU; break;
        case EAST:  yaw = 0.5f   * TAU; break;
    }
    Vec3 axis = {0, 1, 0};
    return quaternionFromAxisAngle(axis, yaw);
}

// takes integer coords and converts to Vec3 before passing to assets_to_load
// scale = 1
// rotation from ROTATION enum to quaternion
// assuming one path -> one asset type.
void drawAsset(char* path, AssetType type, Int3 coords, Direction direction)
{
    // check loaded_assets. if this char, continue at this index. if == 0, put path and type, and continue with this index.
	int32 asset_location = -1;
    for (int32 asset_index = 0; asset_index < 256; asset_index++)
    {
        if (loaded_texture_paths[asset_index] == path)
        {
            asset_location = asset_index;
            break;
        }
        if (loaded_texture_paths[asset_index] == 0)
        {
            loaded_texture_paths[asset_index] = path;
            asset_location = asset_index;
            break;
        }
    }
    assets_to_load[asset_location].path = path;
    assets_to_load[asset_location].type = type;

    switch (type)
    {
        case SPRITE_2D:
            return;
        case CUBE_3D:
            assets_to_load[asset_location].coords[assets_to_load[asset_location].instance_count]   = intCoordsToNorm(coords);
            assets_to_load[asset_location].scale[assets_to_load[asset_location].instance_count]    = DEFAULT_SCALE;
            assets_to_load[asset_location].rotation[assets_to_load[asset_location].instance_count] = directionToQuaternion(direction);
            assets_to_load[asset_location].instance_count++;
            return;
        case MODEL_3D:
            return;
    }
}

void gameInitialise(void) 
{	

}

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;
	accumulator += delta_time;

    camera_yaw   += tick_input.mouse_dx * SENSITIVITY;
    if (camera_yaw >  3.14159265f) camera_yaw -= 2.0f*3.14159265f;
    if (camera_yaw < -3.14159265f) camera_yaw += 2.0f*3.14159265f;

    camera_pitch += tick_input.mouse_dy * SENSITIVITY;
    float pitch_limit = 1.553343f;
    if (camera_pitch >  pitch_limit) camera_pitch =  pitch_limit; 
    if (camera_pitch < -pitch_limit) camera_pitch = -pitch_limit; 

    Vec3 default_quaternion_yaw   = { 0, 1, 0 };
    Vec3 default_quaternion_pitch = { 1, 0, 0 };
    Vec4 quaternion_yaw   = quaternionFromAxisAngle(default_quaternion_yaw,   camera_yaw);
    Vec4 quaternion_pitch = quaternionFromAxisAngle(default_quaternion_pitch, camera_pitch);
    camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));

    while (accumulator >= PHYSICS_INCREMENT)
    {
		// handle movement input

		Vec3 right_camera_basis, forward_camera_basis;
        cameraBasisFromYaw(camera_yaw, &right_camera_basis, &forward_camera_basis);

        if (tick_input.w_press) 
        {
            camera.coords.x += forward_camera_basis.x * MOVE_STEP;
            camera.coords.z += forward_camera_basis.z * MOVE_STEP;
        }
        if (tick_input.a_press) 
        {
            camera.coords.x -= right_camera_basis.x * MOVE_STEP;
            camera.coords.z -= right_camera_basis.z * MOVE_STEP;
        }
        if (tick_input.s_press) 
        {
            camera.coords.x -= forward_camera_basis.x * MOVE_STEP;
            camera.coords.z -= forward_camera_basis.z * MOVE_STEP;
        }
        if (tick_input.d_press) 
        {
            camera.coords.x += right_camera_basis.x * MOVE_STEP;
            camera.coords.z += right_camera_basis.z * MOVE_STEP;
        }
        if (tick_input.space_press) camera.coords.y += 0.05f;
        if (tick_input.shift_press) camera.coords.y -= 0.05f;

		// should be done in drawAsset

        /*
		assets_to_load[0].path = box_path;
        assets_to_load[0].type = CUBE_3D;
        assets_to_load[0].coords[0] = box_coords_1;
        assets_to_load[0].scale[0] = box_scale;
        assets_to_load[0].rotation[0] = box_rotation;
        assets_to_load[0].coords[1] = box_coords_2;
        assets_to_load[0].scale[1] = box_scale;
        assets_to_load[0].rotation[1] = box_rotation;
        assets_to_load[0].instance_count = 2;
		*/

        Int3 box_coords_1   = { 0, 0, 0 };
		Int3 box_coords_2   = { 0, 1, 1 };

        drawAsset(box_path, CUBE_3D, box_coords_1, NORTH);
        drawAsset(box_path, CUBE_3D, box_coords_2, SOUTH);

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
