// A handful of threads for the scene setup code.
//
// Both scenes spend most of their start-up time on the CPU: scene 1 fills three
// 256x256 procedural textures, scene 2 tessellates a few hundred thousand
// vertices of poles, wires, houses and trees. None of that touches GL, and on
// the Vita it would otherwise run on one core of four - while the (unavoidably
// serial) SceShaccCg compiles hold the same core. So the data generation goes to
// worker threads and the shader compilation runs underneath it on the calling
// thread.
//
// Two rules, both relied on by the callers:
//   * workers never touch GL; the caller uploads after WaitAll();
//   * a task must only write memory it owns (each scene's own std::vector, or
//     globals nobody else is reading while it runs).
#pragma once

#include <functional>

namespace jobs {

// Cores this build can use: 4 on the Vita, the host's count elsewhere.
int CoreCount();

// Queue `count` independent tasks, task(i) for i in [0,count), and start them
// on the spare cores. Returns immediately: this thread keeps its own work (the
// shader compiles) running and calls WaitAll() when it needs the results. At
// most CoreCount()-1 worker threads are started, so the caller is the last
// core; any task no worker picked up is run by WaitAll() on this thread.
void Start(int count, const std::function<void(int)> &task);

// Wait for every task queued since the last WaitAll(), then release the threads.
void WaitAll();

}  // namespace jobs
