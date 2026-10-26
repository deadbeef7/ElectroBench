// ElectroBench — PlayStation Portable port, SCENE 4 (power lines / "lain").
#pragma once
void Scene4Init();
void Scene4Update(double t);
void Scene4Camera(float outEye[3], float *yaw, float *pitch);
float Scene4FovY();
void Scene4DrawSky();
void Scene4Draw();
