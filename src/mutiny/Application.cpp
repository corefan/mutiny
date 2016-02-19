#include "Application.h"
#include "GameObject.h"
#include "Component.h"
#include "Camera.h"
#include "Material.h"
#include "Screen.h"
#include "Time.h"
#include "Input.h"
#include "RenderTexture.h"
#include "Resources.h"
#include "GuiSkin.h"
#include "Shader.h"
#include "Debug.h"
#include "Texture2d.h"
#include "Gui.h"
#include "Graphics.h"
#include "Transform.h"
#include "Exception.h"
#include "arc.h"
#include "internal/platform.h"
#include "internal/gcmm.h"

#include "internal/Util.h"
#include "internal/CWrapper.h"

#include <GL/glew.h>

#ifdef USE_SDL
  #include <SDL/SDL.h>
  #include <SDL/SDL_mixer.h>
#endif

#ifdef USE_GLUT
  #include <GL/freeglut.h>
#endif

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#ifdef USE_WINAPI
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <ctime>
#include <functional>
#include <iostream>
#include <fstream>

namespace mutiny
{

namespace engine
{

Context* Application::context = NULL;

void Application::init(int argc, char* argv[])
{
  internal::gc::context* gc_ctx = new internal::gc::context();
  context = gc_ctx->gc_new<Context>();
  context->gc_ctx = gc_ctx;

  context->objects = gc_ctx->gc_list<arc<Object> >();
  context->gameObjects = gc_ctx->gc_list<GameObject*>();
  context->running = false;
  context->argc = argc;

  for(int i = 0; i < argc; i++)
  {
    context->argv.push_back(argv[i]);
  }

  srand(time(NULL));
  setupPaths();

#ifdef USE_SDL
  if(SDL_Init(SDL_INIT_EVERYTHING) == -1)
  {
    std::cout << "Error: Failed to initialize" << std::endl;
    throw std::exception();
  }

  screen = SDL_SetVideoMode(800, 600, 32, SDL_OPENGL | SDL_RESIZABLE);

  if(screen == NULL)
  {
    std::cout << "Error: Failed to create rendering context" << std::endl;
    throw std::exception();
  }

  setTitle("Mutiny Engine");
#endif

#ifdef USE_GLUT
  glutInit(&argc, argv);
  glutInitWindowSize(800, 600);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
  glutCreateWindow("Mutiny Engine");
  glutIdleFunc(idle);
  glutDisplayFunc(display);
  glutReshapeFunc(reshape);
  glutMouseFunc(mouse);
  glutMotionFunc(motion);
  glutPassiveMotionFunc(motion);
  glutKeyboardFunc(keyboard);
  glutKeyboardUpFunc(keyboardUp);
  glutSpecialFunc(_keyboard);
  glutSpecialUpFunc(_keyboardUp);
#endif

  Screen::width = 800;
  Screen::height = 600;

  glewInit();
  //if(glewInit() != 0)
  //{
  //  std::cout << "Error: Failed to initialize OpenGL" << std::endl;
  //  throw std::exception();
  //}

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);

#ifdef USE_SDL
  if(Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 1024) == -1)
  {
    std::cout << "Audio failed to initialize" << std::endl;
    throw std::exception();
  }
#endif

  Texture2d::defaultTexture.reset(new Texture2d(24, 24));
  Object::dontDestroyOnLoad(Texture2d::defaultTexture.cast<Object>());

  for(int y = 0; y < 23; y+=2)
  {
    for(int x = 0; x < 23; x+=2)
    {
      Texture2d::defaultTexture->setPixel(x, y, Color(1, 0, 1, 1));
      Texture2d::defaultTexture->setPixel(x+1, y, Color(0, 0, 0, 1));
      Texture2d::defaultTexture->setPixel(x, y+1, Color(0, 0, 0, 1));
      Texture2d::defaultTexture->setPixel(x+1, y+1, Color(1, 0, 1, 1));
    }
  }

  Texture2d::defaultTexture->apply();

  arc<Shader> shader = Resources::load<Shader>("shaders/internal-mesh-normal");
  if(shader.get() == NULL) { throw std::exception(); }
  Object::dontDestroyOnLoad(shader.cast<Object>());
  Material::meshNormalMaterial.reset(new Material(shader));
  Object::dontDestroyOnLoad(Material::meshNormalMaterial.cast<Object>());

  shader = Resources::load<Shader>("shaders/internal-mesh-normal-texture");
  if(shader.get() == NULL) { throw std::exception(); }
  Object::dontDestroyOnLoad(shader.cast<Object>());
  Material::meshNormalTextureMaterial.reset(new Material(shader));
  Object::dontDestroyOnLoad(Material::meshNormalTextureMaterial.cast<Object>());

  Material::guiMaterial = Resources::load<Material>("shaders/default_gui");
  Object::dontDestroyOnLoad(Material::guiMaterial.cast<Object>());

  Material::particleMaterial = Resources::load<Material>("shaders/default_particle");
  Object::dontDestroyOnLoad(Material::particleMaterial.cast<Object>());

  Graphics::defaultMaterial = Resources::load<Material>("shaders/Internal-GUITexture");
  Object::dontDestroyOnLoad(Graphics::defaultMaterial.cast<Object>());

  GuiSkin::_default = new GuiSkin();
  Material::current = NULL;
  Camera::current = NULL;
  Camera::_main = NULL;
  Gui::skin = NULL;
}

void Application::setTitle(std::string title)
{
#ifdef USE_SDL
  SDL_WM_SetCaption(title.c_str(), NULL);
#endif

#ifdef USE_GLUT
  glutSetWindowTitle(title.c_str());
#endif
}

bool Application::isValidPrefix(std::string path, std::string basename)
{
#ifdef USE_WINAPI
  try
  {
    arc<internal::Win32FindData> findData = internal::Win32FindData::create();
    arc<internal::FindHandle> findHandle = internal::FindHandle::FindFirstFile(path + "\\share\\*", findData);

    do
    {
      if(std::string(findData->ffd.cFileName) == basename)
      {
        return true;
      }
    }
    while(findHandle->FindNextFile() != false);
  }
  catch(std::exception& e) { }

  return false;
#else
  return false;
#endif
}

void Application::setupPaths()
{
  std::string dirname;
  std::string basename;

#ifdef EMSCRIPTEN
  engineDataPath = ".";
  dataPath = ".";
#elif USE_WINAPI
  char strExePath [MAX_PATH];

  GetModuleFileName(NULL, strExePath, MAX_PATH);
  dirname = strExePath;
  basename = dirname.substr(dirname.find_last_of("\\"));
  basename = basename.substr(0, basename.length() - 4);

  while(true)
  {
    if(isValidPrefix(dirname, basename.substr(1)) == true)
    {
      break;
    }

    std::string prev = dirname;
    dirname = dirname.substr(0, dirname.find_last_of("\\"));

    if(dirname == prev)
    {
      throw Exception("Failed to find data directory");
    }
  }

  context->engineDataPath = dirname + "/share/mutiny";
  context->dataPath = dirname + "/share" + basename;
#else
  FILE* process = NULL;
  std::string command;
  char buffer[8];

  command = "cd `dirname \\`which " + std::string(argv.at(0)) + "\\``; cd ..; pwd | tr -d '\n'";
  process = popen(command.c_str(), "r");

  if(process == NULL)
  {
    throw std::exception();
    //throw Exception("Failed to open child process");
  }

  while(fgets(buffer, 7, process) != NULL)
  {
    dirname += buffer;
  }

  pclose(process);
  command = "basename " + std::string(argv.at(0)) + " | tr -d '\n'";
  process = popen(command.c_str(), "r");

  if(process == NULL)
  {
    throw std::exception();
    //throw Exception("Failed to open child process");
  }

  while(fgets(buffer, 7, process) != NULL)
  {
    basename += buffer;
  }

  pclose(process);

  engineDataPath = dirname + "/share/mutiny";
  dataPath = dirname + "/share/" + basename;
#endif

  //std::cout << "Paths: " << engineDataPath << " " << dataPath << std::endl;
}

void Application::destroy()
{
  // TODO: Running is a flag, not a reliable state
  if(context->running == true)
  {
    throw std::exception();
  }

  delete GuiSkin::_default;

  Camera::allCameras.clear();
  context->gameObjects->clear();
  context->paths.clear();
  context->objects->clear();

  Material::meshNormalMaterial.reset();
  Material::meshNormalTextureMaterial.reset();
  Material::guiMaterial.reset();
  Material::particleMaterial.reset();
  Graphics::defaultMaterial.reset();
  Texture2d::defaultTexture.reset();

#ifdef USE_SDL
  SDL_Quit();
#endif

  delete context->gc_ctx;
}

void Application::run()
{
  if(context->running == true)
  {
    return;
  }

  context->running = true;

#ifdef USE_SDL
  #ifdef EMSCRIPTEN
  //loop();
  //emscripten_set_main_loop(loop, 60, true);
  emscripten_set_main_loop(loop, 0, true);
  #else
  while(context->running == true)
  {
    loop();
  }

  #endif
#else
  glutMainLoop();
#endif

  context->running = false;

  for(int i = 0; i < context->gameObjects->size(); i++)
  {
    context->gameObjects->at(i)->destroy();
  }
}

void Application::loop()
{
#ifdef USE_SDL
  SDL_Event event = { 0 };

#ifdef EMSCRIPTEN
  // Does not allow SDL_Delay. Assumes it is in an infinite loop.
#else
  float idealTime = 1.0f / 60.0f;

  if(idealTime > Time::deltaTime)
  {
    // Sleep off remaining time
    SDL_Delay((idealTime - Time::deltaTime) * 1000.0f);
    //usleep((idealTime - Time::deltaTime) * 1000000.0f);
    //Time::deltaTime = idealTime;
  }
#endif

  static float lastTime = SDL_GetTicks();
  float time = SDL_GetTicks();
  float diff = time - lastTime;
  Time::deltaTime = diff / 1000.0f;
  lastTime = time;

  Screen::width = screen->w;
  Screen::height = screen->h;

  while(SDL_PollEvent(&event))
  {
    if(event.type == SDL_QUIT)
    {
      running = false;
    }
    else if(event.type == SDL_VIDEORESIZE)
    {
      reshape(event.resize.w, event.resize.h);
    }
#ifdef EMSCRIPTEN
    else if(event.type == SDL_FINGERMOTION)
    {
      motion(event.tfinger.x * Screen::getWidth(),
             event.tfinger.y * Screen::getHeight());
    }
#endif
    else if(event.type == SDL_MOUSEMOTION)
    {
      motion(event.motion.x, event.motion.y);
    }
#ifdef EMSCRIPTEN
    else if(event.type == SDL_FINGERDOWN)
    {
      motion(event.tfinger.x * Screen::getWidth(),
             event.tfinger.y * Screen::getHeight());

      mouse(0, SDL_MOUSEBUTTONDOWN, Input::mousePosition.x, Input::mousePosition.y);
    }
#endif
    else if(event.type == SDL_MOUSEBUTTONDOWN)
    {
      mouse(event.button.button, SDL_MOUSEBUTTONDOWN, Input::mousePosition.x,
        Input::mousePosition.y);
    }
#ifdef EMSCRIPTEN
    else if(event.type == SDL_FINGERUP)
    {
      motion(event.tfinger.x * Screen::getWidth(),
             event.tfinger.y * Screen::getHeight());

      mouse(0, SDL_MOUSEBUTTONUP, Input::mousePosition.x, Input::mousePosition.y);
    }
#endif
    else if(event.type == SDL_MOUSEBUTTONUP)
    {
      mouse(event.button.button, SDL_MOUSEBUTTONUP, Input::mousePosition.x,
        Input::mousePosition.y);
    }
    else if(event.type == SDL_KEYDOWN)
    {
      _keyboard(event.key.keysym.sym, Input::mousePosition.x, Input::mousePosition.y);
    }
    else if(event.type == SDL_KEYUP)
    {
      _keyboardUp(event.key.keysym.sym, Input::mousePosition.x, Input::mousePosition.y);
    }
  }
#endif

  idle();
  display();
}

void Application::quit()
{
  context->running = false;
  delete context->gc_ctx;
}

internal::gc::context* Application::getGC()
{
  return context->gc_ctx;
}

void Application::loadLevel()
{
  for(int i = 0; i < context->gameObjects->size(); i++)
  {
    if(context->gameObjects->at(i)->destroyOnLoad == true)
    {
      context->gameObjects->at(i)->destroy();
      context->gameObjects->remove_at(i);
      i--;
    }
  }

  for(int i = 0; i < context->objects->size(); i++)
  {
    if(context->objects->at(i)->destroyOnLoad == true)
    {
      context->objects->remove_at(i);
      context->paths.erase(context->paths.begin() + i);
      i--;
    }
  }

  for(int i = 0; i < context->gameObjects->size(); i++)
  {
    context->gameObjects->at(i)->levelWasLoaded();
  }
}

void Application::loadLevel(std::string path)
{
  if(context->running == true)
  {
    context->levelChange = path;
  }
  else
  {
    context->loadedLevelName = path;
    loadLevel();
  }
}

std::string Application::getLoadedLevelName()
{
  return context->loadedLevelName;
}

std::string Application::getDataPath()
{
  return context->dataPath;
}

std::string Application::getEngineDataPath()
{
  return context->engineDataPath;
}

internal::gc::list<GameObject*>* Application::getGameObjects()
{
  return context->gameObjects;
}

int Application::getArgc()
{
  return context->argc;
}

std::string Application::getArgv(int i)
{
  return context->argv.at(i);
}

void Application::reshape(int width, int height)
{
  Screen::width = width;
  Screen::height = height;
#ifdef USE_SDL
  context->screen = SDL_SetVideoMode(Screen::width, Screen::height, 32, SDL_OPENGL | SDL_RESIZABLE);
#endif

  idle();
}

void Application::display()
{
  glClearColor(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(0, 0, Screen::getWidth(), Screen::getHeight());

  for(int h = 0; h < Camera::getAllCameras()->size(); h++)
  {
    if(Camera::getAllCameras()->at(h)->getGameObject()->getActive() == false)
    {
      continue;
    }

    Camera::current = Camera::getAllCameras()->at(h);

    if(Camera::getCurrent()->targetTexture.get() != NULL)
    {
      RenderTexture::setActive(Camera::getCurrent()->targetTexture);
    }

    //glClearColor(0.2f, 0.2f, 0.5f, 1.0f);
    Color clearColor = Camera::getCurrent()->getBackgroundColor();
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for(int i = 0; i < context->gameObjects->size(); i++)
    {
      if((Camera::getCurrent()->getCullMask() & context->gameObjects->at(i)->getLayer()) !=
        context->gameObjects->at(i)->getLayer())
      {
        continue;
      }

      context->gameObjects->at(i)->render();
    }

    if(Camera::getCurrent()->targetTexture.get() != NULL)
    {
      RenderTexture::setActive(arc<RenderTexture>());
    }
  }

  for(int i = 0; i < context->gameObjects->size(); i++)
  {
    context->gameObjects->at(i)->postRender();
  }

  for(int i = 0; i < context->gameObjects->size(); i++)
  {
    context->gameObjects->at(i)->gui();
  }

#ifdef USE_SDL
  SDL_GL_SwapBuffers();
#else
  glutSwapBuffers();
#endif

  Input::downKeys.clear();
  Input::upKeys.clear();
  //Input::downMouseButtons.clear();
  //Input::upMouseButtons.clear();

  if(context->levelChange != "")
  {
    context->loadedLevelName = context->levelChange;
    context->levelChange = "";
    loadLevel();
  }
}

void Application::idle()
{
#ifdef USE_GLUT
  static float lastTime = glutGet(GLUT_ELAPSED_TIME);
  float time = glutGet(GLUT_ELAPSED_TIME);
  float diff = time - lastTime;
  Time::deltaTime = diff / 1000.0f;
  lastTime = time;
#endif

  for(int i = 0; i < context->gameObjects->size(); i++)
  {
    context->gameObjects->at(i)->update();
  }

  for(int i = 0; i < context->gameObjects->size(); i++)
  {
    if(context->gameObjects->at(i)->destroyed == true)
    {
      context->gameObjects->at(i)->destroy();
      context->gameObjects->remove_at(i);
      i--;
    }
  }

#ifdef USE_GLUT
  glutPostRedisplay();
#endif

  Input::downMouseButtons.clear();
  Input::upMouseButtons.clear();

  context->gc_ctx->gc_collect();
}

void Application::motion(int x, int y)
{
  Input::mousePosition.x = x;
  Input::mousePosition.y = y;
}

void Application::mouse(int button, int state, int x, int y)
{
#ifdef USE_SDL
  if(state == SDL_MOUSEBUTTONDOWN)
#endif
#ifdef USE_GLUT
  if(state == GLUT_DOWN)
#endif
  {
    for(int i = 0; i < Input::mouseButtons.size(); i++)
    {
      if(Input::mouseButtons.at(i) == button)
      {
        return;
      }
    }

    Input::mouseDownPosition.x = x;
    Input::mouseDownPosition.y = y;
    Input::mouseButtons.push_back(button);
    Input::downMouseButtons.push_back(button);
  }
  else
  {
    for(int i = 0; i < Input::mouseButtons.size(); i++)
    {
      if(Input::mouseButtons.at(i) == button)
      {
        Input::mouseButtons.erase(Input::mouseButtons.begin() + i);
        i--;
      }
    }

    Input::upMouseButtons.push_back(button);
    //std::cout << (int)button << std::endl;
  }
}

void Application::keyboard(unsigned char key, int x, int y)
{
  _keyboard(key, x, y);
}

void Application::_keyboard(int key, int x, int y)
{
  //std::cout << key << std::endl;
  for(int i = 0; i < Input::keys.size(); i++)
  {
    if(Input::keys.at(i) == key)
    {
      return;
    }
  }

  Input::keys.push_back(key);
  Input::downKeys.push_back(key);
}

void Application::keyboardUp(unsigned char key, int x, int y)
{
  _keyboardUp(key, x, y);
}

void Application::_keyboardUp(int key, int x, int y)
{
  for(int i = 0; i < Input::keys.size(); i++)
  {
    if(Input::keys.at(i) == key)
    {
      Input::keys.erase(Input::keys.begin() + i);
      i--;
    }
  }

  Input::upKeys.push_back(key);
}

}

}

