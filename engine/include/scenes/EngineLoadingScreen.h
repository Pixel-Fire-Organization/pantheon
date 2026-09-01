#pragma once

/// (Re)acquire the configured loading-screen images. Called once at the start
/// of every scene transition, after the runtime reset — the images do not
/// survive that reset, the same as every other resource.
void Engine_LoadingScreen_Begin();

/// Draw the current loading-screen image and a progress meter. A no-op when
/// the Ui subsystem is not enabled or no images are configured.
/// @param progress Fraction in [0,1] of the incoming scene's declared
///        resources now ready.
/// @param dt Seconds since the last call, for cycling between images.
void Engine_LoadingScreen_Draw(float progress, float dt);
