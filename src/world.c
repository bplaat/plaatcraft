// PlaatCraft - World

#include "world.h"
#include <stdlib.h>
#include <math.h>
#include "geometry/block.h"
#include "log.h"

World* world_new(int seed) {
    World* world = malloc(sizeof(World));
    world->seed = seed;

    for (size_t i = 0; i < sizeof(world->chunks) / sizeof(Chunk*); i++) {
        world->chunks[i] = NULL;
    }
    world->chunks_size = 0;

    return world;
}

Chunk* world_get_chunk(World* world, int chunk_x, int chunk_y, int chunk_z) {
    for (int i = 0; i < world->chunks_size; i++) {
        Chunk* chunk = world->chunks[i];
        if (chunk->x == chunk_x && chunk->y == chunk_y && chunk->z == chunk_z) {
            return chunk;
        }
    }

    Chunk* chunk = chunk_new(chunk_x, chunk_y, chunk_z);

    if (world->chunks_size == (sizeof(world->chunks) / sizeof(Chunk*))) {
        world->chunks_size = 0;
    }
    if (world->chunks[world->chunks_size] != NULL) {
        chunk_free(world->chunks[world->chunks_size]);
    }
    world->chunks[world->chunks_size++] = chunk;

    return chunk;
}

void world_render(World* world, Camera* camera, BlockShader* blockShader, TextureAtlas* blocksTextureAtlas) {
    block_shader_enable(blockShader);
    texture_atlas_enable(blocksTextureAtlas);

    glUniformMatrix4fv(blockShader->projection_matrix_uniform, 1, GL_FALSE, &camera->projectionMatrix.m11);
    glUniformMatrix4fv(blockShader->view_matrix_uniform, 1, GL_FALSE, &camera->viewMatrix.m11);

    Matrix4 rotationMatrix;
    matrix4_rotate_x(&rotationMatrix, radians(-90));

    int player_chunk_x = floor(camera->position.x / (float)CHUNK_SIZE);
    int player_chunk_y = floor(camera->position.y / (float)CHUNK_SIZE);
    int player_chunk_z = floor(camera->position.z / (float)CHUNK_SIZE);

    // log_info("Camera %.3g %.3g %.3g", camera->position.x, camera->position.y, camera->position.z);
    // log_info("Chunk %d %d %d", player_chunk_x, player_chunk_y, player_chunk_z);

    int render_distance = 3;
    for (int chunk_z = player_chunk_z - render_distance; chunk_z <= player_chunk_z + render_distance; chunk_z++) {
        for (int chunk_y = player_chunk_y - render_distance; chunk_y <= player_chunk_y + render_distance; chunk_y++) {
            for (int chunk_x = player_chunk_x - render_distance; chunk_x <= player_chunk_x + render_distance; chunk_x++) {

                bool chunk_is_visible = true;

                // int chunk_x_max = chunk_x < 0 ? -CHUNK_SIZE : CHUNK_SIZE;
                // int chunk_y_max = chunk_y < 0 ? -CHUNK_SIZE : CHUNK_SIZE;
                // int chunk_z_max = chunk_z < 0 ? -CHUNK_SIZE : CHUNK_SIZE;

                // Vector4 corners[8] = {
                //     { chunk_x, chunk_y, chunk_z, 1 },
                //     { chunk_x + chunk_x_max, chunk_y, chunk_z, 1 },
                //     { chunk_x, chunk_y + chunk_y_max, chunk_z, 1 },
                //     { chunk_x + chunk_x_max, chunk_y + chunk_y_max, chunk_z, 1 },

                //     { chunk_x, chunk_y, chunk_z + chunk_z_max, 1 },
                //     { chunk_x + chunk_x_max, chunk_y, chunk_z + chunk_z_max, 1 },
                //     { chunk_x, chunk_y + chunk_y_max, chunk_z + chunk_z_max, 1 },
                //     { chunk_x + chunk_x_max, chunk_y + chunk_y_max, chunk_z + chunk_z_max, 1 }
                // };

                // for (size_t i = 0; i < sizeof(corners) / sizeof(Vector4); i++) {
                //     vector4_mul(&corners[i], &camera->viewMatrix);
                //     vector4_mul(&corners[i], &camera->projectionMatrix);

                //     #define within(a, b, c) ((a) >= (b) && (b) <= (c))
                //     if (
                //         within(-corners[i].w, corners[i].x, corners[i].w) &&
                //         within(-corners[i].w, corners[i].y, corners[i].w) &&
                //         within(0, corners[i].z, corners[i].w)
                //     ) {
                //         chunk_is_visible = true;
                //         break;
                //     }
                // }

                if (chunk_is_visible) {
                    Chunk* chunk = world_get_chunk(world, chunk_x, chunk_y, chunk_z);

                    chunk_update_data(chunk, world);

                    for (int block_z = 0; block_z < CHUNK_SIZE; block_z++) {
                        for (int block_y = 0; block_y < CHUNK_SIZE; block_y++) {
                            for (int block_x = 0; block_x < CHUNK_SIZE; block_x++) {
                                uint8_t block_data = chunk->data[block_z * CHUNK_SIZE * CHUNK_SIZE + block_y * CHUNK_SIZE + block_x];
                                if ((block_data & CHUNK_DATA_VISIBLE_BIT) != 0) {
                                    BlockType blockType = block_data & CHUNK_DATA_BLOCK_TYPE;

                                    Matrix4 modelMatrix;
                                    Vector4 blockPosition = {
                                        chunk_x * CHUNK_SIZE + block_x,
                                        chunk_y * CHUNK_SIZE + block_y,
                                        -(chunk_z * CHUNK_SIZE + block_z),
                                        1
                                    };
                                    matrix4_translate(&modelMatrix, &blockPosition);
                                    matrix4_mul(&modelMatrix, &rotationMatrix);

                                    glUniformMatrix4fv(blockShader->model_matrix_uniform, 1, GL_FALSE, &modelMatrix.m11);

                                    glUniform1iv(blockShader->texture_indexes_uniform, 6, (const GLint*)&BLOCK_TEXTURE_FACES[blockType]);

                                    glDrawArrays(GL_TRIANGLES, 0, 36);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    texture_atlas_disable(blocksTextureAtlas);

    block_shader_disable(blockShader);
}

void world_free(World* world) {
    for (size_t i = 0; i < sizeof(world->chunks) / sizeof(Chunk*); i++) {
        if (world->chunks[i] != NULL) {
            chunk_free(world->chunks[i]);
        }
    }

    free(world);
}
