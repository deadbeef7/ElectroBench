#ifndef GL_TEXTURE_H
#define GL_TEXTURE_H

#include "process_image.h"
#include "loadpng.h"

typedef struct Rect {
    float Left, Right, Bottom, Top;
} Rect;

inline void Load_Texture(Image* img, const char* path) {
    Load_Png(&img->img, &img->w, &img->h, path);
}

inline void Load_Texture_Swap(Image* img, const char* path) {
    Load_Png_Swap(&img->img, &img->w, &img->h, path);
}

inline void Map_Texture(Image* img) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img->w, img->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img->img);
}

inline void Draw_Rect(Rect *Rct) {
    glBegin(GL_POLYGON);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f((int)Rct->Left, (int)Rct->Bottom);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f((int)Rct->Right, (int)Rct->Bottom);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f((int)Rct->Right, (int)Rct->Top);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f((int)Rct->Left, (int)Rct->Top);
    glEnd();
}

#endif
