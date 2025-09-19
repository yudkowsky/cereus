#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"

#define local_persist static
#define global_variable static
#define internal static

internal const double PHYSICS_INCREMENT = 1.0/60.0;

typedef struct GameState
{
    WorldState previous_world_state, current_world_state;
    double accumulator;
}
GameState;
internal GameState game_state;

void makeVertex(float pos[2], float color[3], Vertex* out)
{
	out->pos[0] = pos[0];
    out->pos[1] = pos[1];

    out->color[0] = color[0];
    out->color[1] = color[1];
    out->color[2] = color[2];
}

void makeTriangle(Vertex vertices[3], Triangle* out)
{
    out->point1 = vertices[0];
    out->point2 = vertices[1];
    out->point3 = vertices[2];
}

void gameInitialise(void)
{
    game_state.accumulator = 0.0;

	float vertex1_pos[2] = { 0.5f, 0.2f };
    float vertex1_color[3] = { 0.0f, 1.0f, 0.0f };

	float vertex2_pos[2] = { 0.2f, 0.5f };
    float vertex2_color[3] = { 0.0f, 0.0f, 1.0f };

	float vertex3_pos[2] = { 0.5f, 0.8f };
    float vertex3_color[3] = { 1.0f, 0.0f, 0.0f };

    Vertex vertices[3] = {0};
	makeVertex(vertex1_pos, vertex1_color, &vertices[0]);
	makeVertex(vertex2_pos, vertex2_color, &vertices[1]);
	makeVertex(vertex3_pos, vertex3_color, &vertices[2]);

	makeTriangle(vertices, &game_state.current_world_state.triangle);
	game_state.previous_world_state = game_state.current_world_state; // bootstrap snapshots
}

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	game_state.accumulator += delta_time;
    while (game_state.accumulator >= PHYSICS_INCREMENT)
    {
      	game_state.previous_world_state = game_state.current_world_state;

        // change current world state

        game_state.accumulator -= PHYSICS_INCREMENT;
    }

    double interpolation_fraction = game_state.accumulator / PHYSICS_INCREMENT;

	rendererSubmitFrame(&game_state.previous_world_state, &game_state.current_world_state, interpolation_fraction);
}
