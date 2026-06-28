// Libraries to include
#include "../lib/util.hxx"

SDL_Window *window;
SDL_GLContext glContext;

// Anonymous namespace to initialise window
namespace {
void initialiseWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n",
            SDL_GetError());
    std::exit(EXIT_FAILURE);
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  window =
      SDL_CreateWindow(NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       WIDTH, HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  if (window == NULL) {
    fprintf(stderr, "Window could not be created! SDL_Error: %s\n",
            SDL_GetError());
    std::exit(EXIT_FAILURE);
  }

  glContext = SDL_GL_CreateContext(window);
  if (glContext == NULL) {
    fprintf(stderr, "OpenGL context could not be created! SDL Error: %s\n",
            SDL_GetError());
    std::exit(EXIT_FAILURE);
  }

  SDL_GL_SetSwapInterval(0);
}
} // namespace

// Global variables
GLuint vert, frag, program;
float pos_x, pos_y, pos_z;
float angle_x = 30.0f, angle_y = 0.0f;
int init_time = time(NULL), final_time, frame;
int fps;
int x_old = 0, y_old = 0;
int current_scroll = 5;
float zoom_per_scroll;
std::string model_name = "assets/UZI.obj";
bool is_holding_mouse = false;
bool is_updated = false;

Model model;

// @fun textFileRead
// @args: filename : const string
// Reads the contents of a text file.
std::string textFileRead(std::string const &filename) {
  if (!filename.empty()) {
    std::fstream file{filename};
    return {(std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()};
  }

  return {};
}

float a, b;

// Renders the scene (90 UZIs!) and calculates FPS
void renderScene() {
    unsigned int timet = SDL_GetTicks();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glClearColor(1.0, 0.5, 0.001, 1.0);


    int rows = 9;
    int cols = 10;

    float spacing = 0.14f;

    float active_scale = current_scroll * zoom_per_scroll;

    pos_z = -1.18f;
    float camera_shift_x = -0.6f;
    glTranslatef(camera_shift_x, -0.5f, 0.0f);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            glPushMatrix();

            float posOffsetX = j * spacing;
            float posOffsetY = i * spacing;

            glTranslatef(pos_x + posOffsetX, pos_y + posOffsetY, pos_z);

            glRotatef(angle_x + (i + j) * 10.0f, 1.0f, 0.0f, 0.0f);
            glRotatef(angle_y + (i + j) * 15.0f, 0.0f, 1.0f, 0.0f);
            glScalef(active_scale, active_scale, active_scale);
            model.draw();

            glPopMatrix();
        }
    }

    glUniform1f(glGetUniformLocation(program, "timeFactor"),
                (float)SDL_GetPerformanceCounter() /
                    (float)SDL_GetPerformanceFrequency());
    SDL_GL_SwapWindow(window);

    frame++;
    a++;
    b++;
    final_time = time(NULL);
    if (final_time - init_time > 0) {
        char title[256];
        fps = frame / (final_time - init_time);
        snprintf(title, 256, "ElectroBench - FPS : %d", fps);
        SDL_SetWindowTitle(window, title);
        frame = 0;
        init_time = final_time;
    }
    if (timet >= 60000) {
        printf("Benchmark Results - Score : %f\n", (fps * 2) / (1.01 / fps));
        SDL_Quit();
        exit(0);
    }
}

// Processes keyboard input.
void processKeys(SDL_Event &event) {
  if (event.key.keysym.sym == SDLK_ESCAPE) {
    SDL_Quit();
    exit(0);
  }
}

// Prints any OpenGL errors.
#define printOpenGLError() printGLError(__FILE__, __LINE__)
int printGLError(const char *file, int line) {
  GLenum glErr;
  int retCode = 0;

  glErr = glGetError();
  while (glErr != GL_NO_ERROR) {
    printf("glError in file %s @ line %d: %s\n", file, line,
           gluErrorString(glErr));
    retCode = 1;
    glErr = glGetError();
  }
  return retCode;
}

// Prints the compile info log for a shader.
void printShaderInfoLog(GLuint object) {
  constexpr std::size_t MAX_LOG_LENGTH = 10000;
  std::array<char, MAX_LOG_LENGTH> log_buffer{};

  int infologLength = 0;
  glGetShaderiv(object, GL_INFO_LOG_LENGTH, &infologLength);

  if (infologLength > 0) {
    int charsWritten = 0;
    glGetShaderInfoLog(object, MAX_LOG_LENGTH, &charsWritten,
                       log_buffer.data());
    printf("%s\n", log_buffer.data());
  }
}

// Prints the attaching info log for a shader program.
void printProgramInfoLog(GLuint object) {
  int infologLength = 0;
  int charsWritten = 0;
  char *infoLog;

  glGetProgramiv(object, GL_INFO_LOG_LENGTH, &infologLength);

  if (infologLength > 0) {
    infoLog = (char *)malloc(infologLength);
    glGetProgramInfoLog(object, infologLength, &charsWritten, infoLog);
    printf("%s\n", infoLog);
    free(infoLog);
  }
}

void handleMouseEvent(SDL_Event &event) {
  is_updated = true;

  if (event.type == SDL_MOUSEBUTTONDOWN) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      x_old = event.button.x;
      y_old = event.button.y;
      is_holding_mouse = true;
    }
  } else if (event.type == SDL_MOUSEBUTTONUP) {
     if (event.button.button == SDL_BUTTON_LEFT) {
       is_holding_mouse = false;
     }
   }
  else if (event.type == SDL_MOUSEWHEEL) {
    if (event.wheel.y > 0) {
      // Scroll Up: Increase zoom steps
      current_scroll++;
    }
    else if (event.wheel.y < 0) {
      // Scroll Down: Decrease zoom steps
      current_scroll--;
      // Prevent scrolling into negative sizes or inversion
      if (current_scroll < 1) {
        current_scroll = 1;
      }
    }
  }
}


void handleMouseMotion(SDL_Event &event) {
  if (is_holding_mouse) {
    is_updated = true;

    angle_y += (event.motion.x - x_old);
    x_old = event.motion.x;
    if (angle_y > 360.0f)
      angle_y -= 360.0f;
    else if (angle_y < 0.0f)
      angle_y += 360.0f;

    angle_x += (event.motion.y - y_old);
    y_old = event.motion.y;
    if (angle_x > 90.0f)
      angle_x = 90.0f;
    else if (angle_x < -90.0f)
      angle_x = -90.0f;
  }
}

void changeSize(int w, int h) {
  if (h == 0)
    h = 1;

  float ratio = 1.0 * w / h;
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glViewport(0, 0, w, h);
  gluPerspective(45, ratio, 1, 100);
  glMatrixMode(GL_MODELVIEW);
}

/*
 * Initialises the application.
 * Compiles shaders and sets up attributes and loads the obj file.
 */
void setup() {
  GLuint vert = glCreateShader(GL_VERTEX_SHADER);
  if (vert == 0) {
    fprintf(stderr, "Vertex shader not created properly.\n");
    std::terminate();
  }
  GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
  if (frag == 0) {
    fprintf(stderr, "Fragment shader not created properly.\n");
    std::terminate();
  }

  std::string const vs = textFileRead("shaders/vert.glsl");
  std::string const fs = textFileRead("shaders/frag.glsl");

  const char *vs_ptr = vs.data();
  const char *fs_ptr = fs.data();
  glShaderSource(vert, 1, &vs_ptr, NULL);
  glShaderSource(frag, 1, &fs_ptr, NULL);

  glCompileShader(vert);
  glCompileShader(frag);

  printShaderInfoLog(vert);
  printShaderInfoLog(frag);

  GLuint program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);

  glLinkProgram(program);
  printProgramInfoLog(program);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(65.0f, 1.0f, 1.0f, 90.0f);
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glEnable(GL_MULTISAMPLE);
  glHint(GL_MULTISAMPLE_FILTER_HINT_NV, GL_NICEST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  model.load(model_name.c_str());
  pos_x = model.pos_x;
  pos_y = model.pos_y;
  pos_z = -2.0f;
  zoom_per_scroll = -model.pos_z / 8.0f;

  glUseProgram(program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, model.m->texture1);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, model.m->texture2);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, model.m->texture3);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, model.m->texture4);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, model.m->texture5);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, model.m->texture6);
  glUniform1i(glGetUniformLocation(program, "uBaseColor"), 0);
  glUniform1i(glGetUniformLocation(program, "uNormalMap"), 1);
  glUniform1i(glGetUniformLocation(program, "uMetallicMap"), 2);
  glUniform1i(glGetUniformLocation(program, "uHeightMap"), 3);
  glUniform1i(glGetUniformLocation(program, "uAOMap"), 4);
  glUniform1i(glGetUniformLocation(program, "uRoughnessMap"), 5);
}

int main(int argc, char **argv) {
  initialiseWindow();
  glewInit();

  setup();

  SDL_Event event;
  bool quit = false;

  init_time = time(NULL);

  while (!quit) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        quit = true;
      } else if (event.type == SDL_KEYDOWN) {
        processKeys(event);
      } else if (event.type == SDL_MOUSEBUTTONDOWN ||
                 event.type == SDL_MOUSEBUTTONUP ||
                 event.type == SDL_MOUSEWHEEL) {
        handleMouseEvent(event);
      } else if (event.type == SDL_MOUSEMOTION) {
        handleMouseMotion(event);
      } else if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          changeSize(event.window.data1, event.window.data2);
        }
      }
    }

    renderScene();
  }

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
