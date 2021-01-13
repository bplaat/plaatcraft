// PlaatCraft - Texture Atlas

#include "textures/texture_atlas.h"
#include <stdlib.h>
#include <stdbool.h>
#include "stb_image/stb_image.h"
#include "log.h"

TextureAtlas* texture_atlas_new(char* path, int tile_size) {
    TextureAtlas* texture_atlas = malloc(sizeof(TextureAtlas));
    texture_atlas->path = path;
    texture_atlas->tile_size = tile_size;

    int32_t image_width, image_height, image_channels;
    uint8_t* image_buffer = stbi_load(path, &image_width, &image_height, &image_channels, STBI_rgb);
    if (!image_buffer) {
        log_error("Can't load image %s", path);
    }

    int cols = image_width / tile_size;
    int rows = image_height / tile_size;

    int texture_width = tile_size;
    int texture_height = (rows * cols) * tile_size;
    uint8_t* texture_buffer = malloc(texture_height * texture_width * STBI_rgb);

    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            for (int py = 0; py < tile_size; py++) {
                for (int px = 0; px < tile_size; px++) {
                    int image_tile_position_x = x * tile_size + px;
                    int image_tile_position_y = y * tile_size + py;
                    int image_tile_position = (image_tile_position_y * image_width + image_tile_position_x) * 3;

                    int texture_tile_position_x = px;
                    int texture_tile_position_y = (y * cols + x) * tile_size + py;
                    int texture_tile_position = (texture_tile_position_y * texture_width + texture_tile_position_x) * 3;

                    texture_buffer[texture_tile_position] = image_buffer[image_tile_position];
                    texture_buffer[texture_tile_position + 1] = image_buffer[image_tile_position + 1];
                    texture_buffer[texture_tile_position + 2] = image_buffer[image_tile_position + 2];
                }
            }
        }
    }

    glGenTextures(1, &texture_atlas->texture_array);
    texture_atlas_enable(texture_atlas);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(image_buffer);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB, tile_size, tile_size, cols * rows, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_buffer);

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    free(texture_buffer);

    texture_atlas_disable(texture_atlas);

    return texture_atlas;
}

void texture_atlas_enable(TextureAtlas* texture_atlas) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_atlas->texture_array);
}

void texture_atlas_disable(TextureAtlas* texture_atlas) {
    (void)texture_atlas;
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void texture_atlas_free(TextureAtlas* texture_atlas) {
    glDeleteTextures(1, &texture_atlas->texture_array);
    free(texture_atlas);
}
