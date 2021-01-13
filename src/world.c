// PlaatCraft - World

#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "geometry/block.h"
#include "perlin/perlin.h"
#include "log.h"

World* world_new(int64_t seed) {
    World* world = malloc(sizeof(World));
    world->seed = seed;
    world->is_wireframed = false;
    world->is_flat_shaded = false;
    #if DEBUG
        world->render_distance = WORLD_RENDER_DISTANCE_FAR;
    #else
        world->render_distance = WORLD_RENDER_DISTANCE_NEAR;
    #endif

    // Init chuch chache
    for (size_t i = 0; i < sizeof(world->chunk_cache) / sizeof(Chunk*); i++) {
        world->chunk_cache[i] = NULL;
    }
    world->chunk_cache_start = 0;
    mtx_init(&world->chunk_cache_lock, mtx_plain);

    // Init request queue
    for (size_t i = 0; i < sizeof(world->request_queue) / sizeof(WorldRequest*); i++) {
        world->request_queue[i] = NULL;
    }
    world->request_queue_size = 0;
    mtx_init(&world->request_queue_lock, mtx_plain);

    // Init World Database
    mtx_init(&world->database_lock, mtx_plain);
    if (sqlite3_open("assets/world.db", &world->database) != SQLITE_OK) {
        log_error("Can't open the SQLLite database:\n%s", sqlite3_errmsg(world->database));
    }

    // Init database world settings table
    char *error_message = NULL;
    if (sqlite3_exec(world->database, "CREATE TABLE IF NOT EXISTS [settings] ("
        "[key] VARCHAR(32) NOT NULL,"
        "[value] VARCHAR(255) NOT NULL"
    ")", 0, 0, &error_message) != SQLITE_OK) {
        log_error("Can't create the world settings table:\n%s", error_message);
    }

    // Select world seed
    sqlite3_stmt* seed_select_statement;
    if (sqlite3_prepare_v2(world->database, "SELECT [value] FROM [settings] WHERE [key] = 'seed'", -1, &seed_select_statement, NULL) != SQLITE_OK) {
        log_error("Can't create select world seed statement");
    }

    if (sqlite3_step(seed_select_statement) == SQLITE_ROW) {
        // When found the world seed set world seed
        const uint8_t* seed = sqlite3_column_text(seed_select_statement, 0);
        world->seed = atoi((char*)seed);
    } else {
        // Else save world seed
        sqlite3_stmt* seed_insert_statement;
        if (sqlite3_prepare_v2(world->database, "INSERT INTO [settings] ([key], [value]) VALUES ('seed', ?)", -1, &seed_insert_statement, NULL) != SQLITE_OK) {
            log_error("Can't create select world seed statement");
        }
        char seed_string_buffer[64];
        sprintf(seed_string_buffer, "%ld", world->seed);
        sqlite3_bind_text(seed_insert_statement, 1, seed_string_buffer, strlen(seed_string_buffer), SQLITE_STATIC);
        if (sqlite3_step(seed_insert_statement) != SQLITE_DONE) {
            log_error("Can't insert world seed into database");
        }
        sqlite3_finalize(seed_insert_statement);
    }
    sqlite3_finalize(seed_select_statement);

    // Init database world chunks table
    if (sqlite3_exec(world->database, "CREATE TABLE IF NOT EXISTS [chunks] ("
        "[x] INT NOT NULL,"
        "[y] INT NOT NULL,"
        "[z] INT NOT NULL,"
        "[data] BLOB NOT NULL"
    ")", 0, 0, &error_message) != SQLITE_OK) {
        log_error("Can't create the world chunks table:\n%s", error_message);
    }

    // Init chunk select statement
    if (sqlite3_prepare_v2(world->database, "SELECT [data] FROM [chunks] WHERE [x] = ? AND [y] = ? AND [z] = ?", -1, &world->chunk_select_statement, NULL) != SQLITE_OK) {
        log_error("Can't create select chunks statement");
    }

    // Init chunk insert statement
    if (sqlite3_prepare_v2(world->database, "INSERT INTO [chunks] ([x], [y], [z], [data]) VALUES (?, ?, ?, ?)", -1, &world->chunk_insert_statement, NULL) != SQLITE_OK) {
        log_error("Can't create insert chunks statement");
    }

    // Init chunk update statement
    if (sqlite3_prepare_v2(world->database, "UPDATE [chunks] SET [data] = ? WHERE [x] = ? AND [y] = ? AND [z] = ?", -1, &world->chunk_update_statement, NULL) != SQLITE_OK) {
        log_error("Can't create update chunks statement");
    }

    // Init workers
    world->worker_running = true;
    mtx_init(&world->worker_running_lock, mtx_plain);
    for (size_t i = 0; i < sizeof(world->worker_threads) / sizeof(thrd_t); i++) {
        thrd_create(&world->worker_threads[i], world_worker_thread, world);
    }

    // Init perlin noise with seed
    perlin_init(world->seed);

    return world;
}

void world_add_chunk_to_cache(World* world, Chunk* chunk) {
    mtx_lock(&world->chunk_cache_lock);
    if (world->chunk_cache_start == (sizeof(world->chunk_cache) / sizeof(Chunk*))) {
        world->chunk_cache_start = 0;
    }
    if (world->chunk_cache[world->chunk_cache_start] != NULL) {
        chunk_free(world->chunk_cache[world->chunk_cache_start]);
    }
    world->chunk_cache[world->chunk_cache_start++] = chunk;
    mtx_unlock(&world->chunk_cache_lock);
}

Chunk* world_get_chunk(World* world, int chunk_x, int chunk_y, int chunk_z) {
    // Check if chunk is in cunk cache
    for (size_t i = 0; i < sizeof(world->chunk_cache) / sizeof(Chunk*); i++) {
        Chunk* chunk = world->chunk_cache[i];

        if (chunk == NULL) {
            break;
        }

        if (chunk->x == chunk_x && chunk->y == chunk_y && chunk->z == chunk_z) {
            return chunk;
        }
    }

    // Select / Search chunk in database
    mtx_lock(&world->database_lock);
    sqlite3_reset(world->chunk_select_statement);
    sqlite3_bind_int(world->chunk_select_statement, 1, chunk_x);
    sqlite3_bind_int(world->chunk_select_statement, 2, chunk_y);
    sqlite3_bind_int(world->chunk_select_statement, 3, chunk_z);
    if (sqlite3_step(world->chunk_select_statement) == SQLITE_ROW) {
        // When found create chunk from data
        const uint8_t* chunk_data = sqlite3_column_blob(world->chunk_select_statement, 0);
        Chunk* chunk = chunk_new_from_data(chunk_x, chunk_y, chunk_z, false, (uint8_t*)chunk_data, true);
        mtx_unlock(&world->database_lock);

        // Update the chunk
        chunk_update(chunk, world);

        // Add chunk to world cache
        world_add_chunk_to_cache(world, chunk);
        return chunk;
    }
    mtx_unlock(&world->database_lock);

    // When not found generate chunk
    Chunk* chunk = chunk_new_from_generator(chunk_x, chunk_y, chunk_z, true);

    // Insert chunk in database
    mtx_lock(&world->database_lock);
    sqlite3_reset(world->chunk_insert_statement);
    sqlite3_bind_int(world->chunk_insert_statement, 1, chunk->x);
    sqlite3_bind_int(world->chunk_insert_statement, 2, chunk->y);
    sqlite3_bind_int(world->chunk_insert_statement, 3, chunk->z);
    sqlite3_bind_blob(world->chunk_insert_statement, 4, chunk->data, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, SQLITE_TRANSIENT);
    if (sqlite3_step(world->chunk_insert_statement) != SQLITE_DONE) {
        log_error("Can't insert chunk into database");
    }
    mtx_unlock(&world->database_lock);

    // Add chunk to world cache
    world_add_chunk_to_cache(world, chunk);
    return chunk;
}

Chunk* world_request_chunk(World* world, int chunk_x, int chunk_y, int chunk_z) {
    // Check if chunk is in cunk cache
    for (size_t i = 0; i < sizeof(world->chunk_cache) / sizeof(Chunk*); i++) {
        Chunk* chunk = world->chunk_cache[i];

        if (chunk == NULL) {
            break;
        }

        if (chunk->x == chunk_x && chunk->y == chunk_y && chunk->z == chunk_z) {
            return chunk;
        }
    }

    // Check for an pending chunk new request
    for (int i = 0; i < world->request_queue_size; i++) {
        WorldRequest* request = world->request_queue[i];
        if (
            request->type == WORLD_REQUEST_TYPE_CHUNK_NEW &&
            request->arguments.chunk_position.x == chunk_x &&
            request->arguments.chunk_position.y == chunk_y &&
            request->arguments.chunk_position.z == chunk_z
        ) {
            return NULL;
        }
    }

    // Create chunk new request and push it to the request queue
    mtx_lock(&world->request_queue_lock);
    WorldRequest* request = malloc(sizeof(WorldRequest));
    request->type = WORLD_REQUEST_TYPE_CHUNK_NEW;
    request->arguments.chunk_position.x = chunk_x;
    request->arguments.chunk_position.y = chunk_y;
    request->arguments.chunk_position.z = chunk_z;
    world->request_queue[world->request_queue_size++] = request;
    mtx_unlock(&world->request_queue_lock);
    return NULL;
}

void world_request_chunk_update(World* world, Chunk* chunk) {
    // Check for an pending chunk update request
    for (int i = 0; i < world->request_queue_size; i++) {
        WorldRequest* request = world->request_queue[i];
        if (
            request->type == WORLD_REQUEST_TYPE_CHUNK_UPDATE &&
            request->arguments.chunk_pointer == chunk
        ) {
            return;
        }
    }

    // Create chunk update request and push it to the request queue
    mtx_lock(&world->request_queue_lock);
    WorldRequest* request = malloc(sizeof(WorldRequest));
    request->type = WORLD_REQUEST_TYPE_CHUNK_UPDATE;
    request->arguments.chunk_pointer = chunk;
    world->request_queue[world->request_queue_size++] = request;
    mtx_unlock(&world->request_queue_lock);
}

int world_render(World* world, Camera* camera, BlockShader* block_shader, TextureAtlas* blocks_texture_atlas) {
    block_shader_enable(block_shader);
    texture_atlas_enable(blocks_texture_atlas);

    if (world->is_wireframed) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    glUniform1i(block_shader->is_lighted_uniform, true);
    glUniform1i(block_shader->is_flad_shaded_uniform, world->is_flat_shaded);

    glUniformMatrix4fv(block_shader->projection_matrix_uniform, 1, GL_FALSE, &camera->projectionMatrix.m11);
    glUniformMatrix4fv(block_shader->camera_matrix_uniform, 1, GL_FALSE, &camera->cameraMatrix.m11);

    Matrix4 rotationMatrix;
    matrix4_rotate_x(&rotationMatrix, radians(90));

    int player_chunk_x = floor(camera->position.x / (float)CHUNK_SIZE);
    int player_chunk_y = floor(camera->position.y / (float)CHUNK_SIZE);
    int player_chunk_z = floor(camera->position.z / (float)CHUNK_SIZE);

    int rendered_chunks = 0;

    for (int chunk_z = player_chunk_z + world->render_distance; chunk_z > player_chunk_z - world->render_distance; chunk_z--) {
        for (int chunk_y = player_chunk_y - world->render_distance; chunk_y <= player_chunk_y + world->render_distance; chunk_y++) {
            for (int chunk_x = player_chunk_x - world->render_distance; chunk_x <= player_chunk_x + world->render_distance; chunk_x++) {
                bool chunk_is_visible = true;

                // #define chunk_min(a) ((a) * CHUNK_SIZE - 0.5)
                // #define chunk_max(a) ((a) * CHUNK_SIZE + CHUNK_SIZE + 0.5)
                // Vector4 corners[8] = {
                //     { chunk_min(chunk_x), chunk_min(chunk_y), -chunk_min(chunk_z), 1 },
                //     { chunk_max(chunk_x), chunk_min(chunk_y), -chunk_min(chunk_z), 1 },
                //     { chunk_min(chunk_x), chunk_max(chunk_y), -chunk_min(chunk_z), 1 },
                //     { chunk_max(chunk_x), chunk_max(chunk_y), -chunk_min(chunk_z), 1 },

                //     { chunk_min(chunk_x), chunk_min(chunk_y), -chunk_max(chunk_z), 1 },
                //     { chunk_max(chunk_x), chunk_min(chunk_y), -chunk_max(chunk_z), 1 },
                //     { chunk_min(chunk_x), chunk_max(chunk_y), -chunk_max(chunk_z), 1 },
                //     { chunk_max(chunk_x), chunk_max(chunk_y), -chunk_max(chunk_z), 1 }
                // };

                // for (size_t i = 0; i < sizeof(corners) / sizeof(Vector4); i++) {
                //     vector4_mul(&corners[i], &camera->cameraMatrix);
                //     vector4_mul(&corners[i], &camera->projectionMatrix);

                //     #define within(a, b, c) ((a) >= (b) && (b) <= (c))
                //     if (
                //         within(-corners[i].w, corners[i].x, corners[i].w) &&
                //         within(-corners[i].w, corners[i].y, corners[i].w)
                //         // && within(0, corners[i].z, corners[i].w)
                //     ) {
                //         chunk_is_visible = true;
                //         break;
                //     }
                // }

                if (chunk_is_visible) {
                    Chunk* chunk = world_request_chunk(world, chunk_x, chunk_y, chunk_z);
                    if (chunk != NULL) {
                        if (chunk->is_changed) {
                            world_request_chunk_update(world, chunk);
                        } else {
                            bool isABlockVisible = false;

                            for (int block_z = 0; block_z < CHUNK_SIZE; block_z++) {
                                for (int block_y = 0; block_y < CHUNK_SIZE; block_y++) {
                                    for (int block_x = 0; block_x < CHUNK_SIZE; block_x++) {
                                        uint8_t block_data = chunk->data[block_z * CHUNK_SIZE * CHUNK_SIZE + block_y * CHUNK_SIZE + block_x];
                                        if ((block_data & CHUNK_DATA_VISIBLE_BIT) != 0) {
                                            isABlockVisible = true;

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

                                            glUniformMatrix4fv(block_shader->model_matrix_uniform, 1, GL_FALSE, &modelMatrix.m11);
                                            glUniform1iv(block_shader->texture_indexes_uniform, 6, (const GLint*)&BLOCK_TYPE_TEXTURE_FACES[blockType]);
                                            glDrawArrays(GL_TRIANGLES, 0, BLOCK_VERTICES_COUNT);
                                        }
                                    }
                                }
                            }

                            if (isABlockVisible) {
                                rendered_chunks++;
                            }
                        }
                    }
                }
            }
        }
    }

    if (world->is_wireframed) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    texture_atlas_disable(blocks_texture_atlas);
    block_shader_disable(block_shader);

    return rendered_chunks;
}

void world_free(World* world) {
    mtx_lock(&world->worker_running_lock);
    world->worker_running = false;
    mtx_unlock(&world->worker_running_lock);

    for (size_t i = 0; i < sizeof(world->worker_threads) / sizeof(thrd_t); i++) {
        thrd_join(world->worker_threads[i], NULL);
    }

    for (size_t i = 0; i < sizeof(world->chunk_cache) / sizeof(Chunk*); i++) {
        Chunk* chunk = world->chunk_cache[i];
        if (chunk != NULL) {
            chunk_free(chunk);
        }
    }

    sqlite3_finalize(world->chunk_select_statement);
    sqlite3_finalize(world->chunk_insert_statement);
    sqlite3_finalize(world->chunk_update_statement);
    sqlite3_close(world->database);

    free(world);
}

int world_worker_thread(void* argument) {
    World* world = (World*)argument;

    while (world->worker_running) {
        if (world->request_queue_size > 0) {
            // Get first request and remove it by shifting
            mtx_lock(&world->request_queue_lock);

            WorldRequest* request = world->request_queue[0];

            for (int i = 1; i < world->request_queue_size; i++) {
                world->request_queue[i - 1] = world->request_queue[i];
            }
            world->request_queue_size--;

            mtx_unlock(&world->request_queue_lock);

            // Create chunk
            if (request->type == WORLD_REQUEST_TYPE_CHUNK_NEW) {
                world_get_chunk(
                    world,
                    request->arguments.chunk_position.x,
                    request->arguments.chunk_position.y,
                    request->arguments.chunk_position.z
                );
            }

            // Update chunk
            if (request->type == WORLD_REQUEST_TYPE_CHUNK_UPDATE) {
                Chunk* chunk = request->arguments.chunk_pointer;
                chunk_update(chunk, world);

                // Update chunk in database
                mtx_lock(&world->database_lock);
                sqlite3_reset(world->chunk_update_statement);
                sqlite3_bind_blob(world->chunk_update_statement, 1, chunk->data, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, SQLITE_TRANSIENT);
                sqlite3_bind_int(world->chunk_update_statement, 2, chunk->x);
                sqlite3_bind_int(world->chunk_update_statement, 3, chunk->y);
                sqlite3_bind_int(world->chunk_update_statement, 4, chunk->z);
                if (sqlite3_step(world->chunk_update_statement) != SQLITE_DONE) {
                    log_error("Can't update chunk in database");
                }
                mtx_unlock(&world->database_lock);
            }

            // Free request
            free(request);
        } else {
            struct timespec duration = { 0, WORLD_WORKER_THREAD_UPDATE_TIMEOUT * 1000 };
            thrd_sleep(&duration, NULL);
        }
    }

    return EXIT_SUCCESS;
}
