/*
 * regame: recycling game
 * Copyright(c) 2003 by wave++ "Yuri D'Elia" <wavexx@users.sf.net>
 * Distributed under GNU LGPL WITHOUT ANY WARRANTY.
 */

/*
 * Headers
 */

// GUI
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>
#include "score.hh"

// graphics
#include <png.h>
#include <FL/gl.h>
#include <FL/glu.h>
#include <FL/fl_draw.H>

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#endif

// poor man's gl_ext
#ifndef GL_TEXTURE_RECTANGLE_ARB
#define GL_TEXTURE_RECTANGLE_ARB 0x84F5
#endif

// base libs
#include <vector>
using std::vector;

#include <string>
using std::string;

#include <map>
using std::map;

#include <fstream>
using std::ifstream;

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <fenv.h>

// time
#if (defined(__MINGW32__) && __GNUG__ > 3) || !defined(WIN32)
#include <sys/time.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


/*
 * Structures
 */

typedef map<string, string> string_map;
typedef int ObjType;

struct Point2f
{
  Point2f()
  {}

  Point2f(float x, float y)
  : x(x), y(y)
  {}

  float x;
  float y;
};


struct Sprite
{
  GLuint tex;
  int w, h;
  float rw, rh;
};


struct PointAcc2f: public Point2f
{
  PointAcc2f()
  {}

  PointAcc2f(float x, float y, float sx, float sy)
  : Point2f(x, y), sx(sx), sy(sy)
  {}

  float sx;
  float sy;
};


struct Container
{
  ObjType accept;
  Point2f pos;
  Sprite s;
  Point2f accWin[2];
  int shakeStart;
};


struct Particle: public PointAcc2f
{
  Particle()
  {}

  Particle(float x, float y, float sx, float sy)
  : PointAcc2f(x, y, sx, sy)
  {}

  ObjType type;
  bool grabbed;
  float maxSpeed;
  int rand;
};



/*
 * Constants
 */

namespace
{
  const char gameData[] = "game.txt";
  const float refms = 1. / 60.;
  const float popupTime = 2.;
  const Fl_Font font = FL_HELVETICA_BOLD;
  const int fontSize = 24;
  const int fontSpc = 2;
  const int startLives = 3;
  const char scoreUrl[] = "http://www.develer.com/~wavexx/regame/score?magic=";
  GLenum target = GL_TEXTURE_RECTANGLE_ARB;
}


/*
 * Utilities
 */


#ifdef WIN32_LEAN_AND_MEAN
int
gettimeofday(struct timeval* tv, struct timezone*)
{
  LARGE_INTEGER n;
  LARGE_INTEGER freq;
  QueryPerformanceCounter(&n);
  QueryPerformanceFrequency(&freq);
  tv->tv_sec = (long)(static_cast<double>(n.QuadPart) / freq.QuadPart);
  tv->tv_usec = (long)((static_cast<double>(n.QuadPart) / freq.QuadPart
	  - tv->tv_sec) * 1000000);
  return 0;
}
#endif


long
tvdiff(const timeval& l, const timeval& r)
{
  return ((l.tv_sec * 1000 + l.tv_usec / 1000) -
      (r.tv_sec * 1000 + r.tv_usec / 1000));
}


const char*
getResDir()
{
  static char buf[PATH_MAX] = "\0";
  if(buf[0]) return buf;

#ifdef __APPLE__
  CFURLRef url = CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle());
  FSRef path;
  CFURLGetFSRef(url, &path);
  CFRelease(url);
  FSRefMakePath(&path, (UInt8*)buf, PATH_MAX);
#elif defined(WIN32)
  strcpy(buf, ".");
#else
  strcpy(buf, GAMEDIR);
#endif

  return buf;
}


float
defaultValue(const string_map& settings,
    const string& setting, const float def)
{
  string_map::const_iterator st = settings.find(setting);
  return (st == settings.end()? def: atof(st->second.c_str()));
}


string
defaultValue(const string_map& settings,
    const string& setting, const string& def)
{
  string_map::const_iterator st = settings.find(setting);
  return (st == settings.end()? def: st->second);
}


bool
loadPairs(string_map& buf, const char* file)
{
  ifstream in(file);
  if(!in) return true;

  string line;
  while(std::getline(in, line))
  {
    // empty lines
    if(!line.size())
      continue;
    if(line[0] == '#')
      continue;

    string::size_type eq = line.find('=');
    if(eq == string::npos || eq == 0)
      return true;

    // insert the new element
    buf.insert(make_pair(line.substr(0, eq), line.substr(eq + 1)));
  }

  return false;
}


GLfloat*
parseColor(GLfloat* buf, const char* color)
{
  // parse the value
  unsigned long v = strtoul((color[0] == '#'? color + 1: color), NULL, 16);

  // separate the components
  buf[0] = static_cast<float>((v >> 16) & 0xFF) / 255.;
  buf[1] = static_cast<float>((v >> 8) & 0xFF) / 255.;
  buf[2] = static_cast<float>((v) & 0xFF) / 255.;

  return buf;
}


int
kpLR(int key)
{
  switch(key)
  {
  case FL_Left:
  case 'a':
  case '4' + FL_KP:
    return FL_Left;

  case FL_Right:
  case 'd':
  case '6' + FL_KP:
    return FL_Right;
  }

  return 0;
}


int nextPower(int i)
{
  int r = 1;
  while((r <<= 1) < i);
  return r;
}


bool
loadTex(Sprite& sprite, const char* file, bool alpha)
{
  FILE* fd = fopen(file, "rb");
  if(!fd) return true;

  png_structp png_ptr = png_create_read_struct(
      PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(!png_ptr) return true;

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if(!info_ptr)
  {
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    return true;
  }

  png_infop end_info = png_create_info_struct(png_ptr);
  if(!end_info)
  {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return true;
  }

  char* buf = NULL;
  char** rows = NULL;

  png_init_io(png_ptr, fd);
  if(setjmp(png_jmpbuf(png_ptr)))
  {
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
    fclose(fd);
    if(buf) delete[] buf;
    if(rows) delete[] rows;
    return true;
  }

  png_read_info(png_ptr, info_ptr);
  sprite.w = png_get_image_width(png_ptr, info_ptr);
  sprite.h = png_get_image_height(png_ptr, info_ptr);

  // enforce 8bit RGB/A.
  png_set_gray_to_rgb(png_ptr);
  png_set_palette_to_rgb(png_ptr);
  png_set_expand(png_ptr);
  png_set_strip_16(png_ptr);
  if(!alpha) png_set_strip_alpha(png_ptr);

  GLenum f = (alpha? GL_RGBA: GL_RGB);
  int chans = (alpha? 4: 3);

  buf = new char[sprite.w * sprite.h * chans];
  rows = new char*[sprite.h];
  if(!buf || !rows) longjmp(png_jmpbuf(png_ptr), 1);

  for(int y = 0; y != sprite.h; ++y)
    rows[y] = buf + (sprite.w * y * chans);

  png_read_image(png_ptr, (png_byte**)rows);
  png_read_end(png_ptr, end_info);
  png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
  fclose(fd);

  glGenTextures(1, &sprite.tex);
  glBindTexture(target, sprite.tex);
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if(target == GL_TEXTURE_RECTANGLE_ARB)
  {
    sprite.rw = sprite.w;
    sprite.rh = sprite.h;
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, f, sprite.w, sprite.h,
	0, f, GL_UNSIGNED_BYTE, buf);
  }
  else
  {
    int tw = nextPower(sprite.w);
    int th = nextPower(sprite.h);
    sprite.rw = static_cast<float>(sprite.w) / tw;
    sprite.rh = static_cast<float>(sprite.h) / th;

    // copy to an aligned nbuffer
    char* nbuf = new char[tw * th * chans];
    for(int y = 0; y != sprite.h; ++y)
      memcpy(
	  nbuf + tw * chans * y,
	  buf + sprite.w * chans * y,
	  sprite.w * chans);

    // clamp to elimiate bleeding
    for(int y = 0; y != sprite.h; ++y)
      for(int x = sprite.w; x != tw; ++x)
	memcpy(
	    nbuf + tw * chans * y + x * chans,
	    nbuf + tw * chans * y + sprite.w * chans - chans,
	    chans);
    for(int y = sprite.h; y != th; ++y)
      memcpy(
	  nbuf + tw * chans * y,
	  nbuf + tw * chans * (sprite.h - 1),
	  tw * chans);

    glTexImage2D(GL_TEXTURE_2D, 0, f, tw, th, 0, f, GL_UNSIGNED_BYTE, nbuf);
    delete []nbuf;
  }

  delete[] rows;
  delete[] buf;
  return false;
}


bool
loadTex2(Sprite& sprite, const char* file, bool alpha)
{
  // loading errors of textures is ignored...
  if(loadTex(sprite, file, alpha))
  {
    fprintf(stderr, "cannot load texture %s\n", file);
    return true;
  }
  return false;
}



/*
 * Implementation
 */

struct Level
{
  // physics (pixels/msec)
  float grav;
  float maxFallSpeed;
  float maxPlayerSpeed;
  float playerAccel;
  float playerFpms;
  float minSpeed;

  // general params
  string title;
  int w, h;
  float mms;
  float mmd;
  int baseline;
  int topline;
  GLfloat color[3];
  int shakeLen;
  int shake;
  int fallWin[2];

  // texture paths
  string backPrefix;
  string cntsPrefix;
  string objsPrefix;
  string playerPrefix;

  // objects
  Sprite back;
  PointAcc2f player;
  vector<Container> cnts;
  vector<Sprite> playerAnim;
  vector<Sprite> objs;
};


bool
loadLevel(Level& data, const char* file)
{
  string_map sm;
  if(loadPairs(sm, file))
    return true;

  data.title = defaultValue(sm, "title", "title");
  data.grav = defaultValue(sm, "grav", 0.001);
  data.maxFallSpeed = defaultValue(sm, "maxFallSpeed", 0.2);
  data.minSpeed = defaultValue(sm, "minSpeed", 0.2);
  data.maxPlayerSpeed = defaultValue(sm, "maxPlayerSpeed", 0.3);
  data.playerAccel = defaultValue(sm, "playerAccel", 0.001);
  parseColor(data.color, defaultValue(sm, "color", "#FF0000").c_str());
  data.w = defaultValue(sm, "w", 640);
  data.h = defaultValue(sm, "h", 480);
  data.mms = defaultValue(sm, "mms", 3000);
  data.mmd = defaultValue(sm, "mmd", 2000);
  data.player.y = defaultValue(sm, "y", 40);
  data.baseline = defaultValue(sm, "baseline", 10);
  data.topline = defaultValue(sm, "topline", 300);
  data.backPrefix = defaultValue(sm, "back", "back");
  data.cntsPrefix = defaultValue(sm, "cntsPrefix", "cnts");
  data.objsPrefix = defaultValue(sm, "objsPrefix", "objs");
  data.playerPrefix = defaultValue(sm, "plyrPrefix", "plyr");
  data.playerAnim.resize(defaultValue(sm, "plyrs", 1));
  data.playerFpms = defaultValue(sm, "plyrFpms", 80.);
  data.shakeLen = defaultValue(sm, "shakeLen", 100);
  data.shake = defaultValue(sm, "shake", 10);
  data.fallWin[0] = defaultValue(sm, "fallx1", 20);
  data.fallWin[1] = defaultValue(sm, "fallx2", 600);

  int n = defaultValue(sm, "cnts", 3);
  data.cnts.resize(n);
  data.objs.resize(n);
  for(int i = 0; i != n; ++i)
  {
    string buf("cnt");
    buf += '0' + i; // ;)
    data.cnts[i].accept = defaultValue(sm, buf + "t", i);
    data.cnts[i].pos.x = defaultValue(sm, buf + "x", 0);
    data.cnts[i].pos.y = defaultValue(sm, buf + "y", 0);
    data.cnts[i].accWin[0].x = defaultValue(sm, buf + "ax1", 0);
    data.cnts[i].accWin[0].y = defaultValue(sm, buf + "ay1", 0);
    data.cnts[i].accWin[1].x = defaultValue(sm, buf + "ax2", 0);
    data.cnts[i].accWin[1].y = defaultValue(sm, buf + "ay2", 0);
  }

  return false;
}


class Regame: public Fl_Gl_Window
{
  const string dataDir;
  Level data;

  // game state
  timeval first;
  timeval now;
  timeval last;
  bool started;
  int startms;
  int score;
  int lives;
  float mms;
  float mmd;
  int pts;

  // game state
  int oldDir;
  vector<Particle> particles;
  bool grabbed;
  int grabType;
  int key;
  int toNext;

  // gui
  Score scoreWin;
  static void _popup(void* data);

  // utilities
  void start();
  void stop();
  void initGL();
  void update();
  void gameover();
  static void _update(void* data);

  void gl_draw_cx(const char* str, const int y);
  void gl_sprite(const Sprite& s, const Point2f& p, const float a = 1.);
  void gl_sprite2(const Sprite& s, const Point2f& p);

public:
  Regame(const char* dataDir, const Level* data);
  ~Regame();

  void reset();
  void draw();
  int handle(int ev);
};


Regame::Regame(const char* dataDir, const Level* data)
: Fl_Gl_Window(data->w, data->h, data->title.c_str()),
  dataDir(dataDir), data(*data)
{
  mode(FL_RGB | FL_DOUBLE);
  reset();
}


void
Regame::stop()
{
  Fl::remove_timeout(_update);
  Fl::remove_timeout(_popup);
}


Regame::~Regame()
{
  stop();
}


void
Regame::start()
{
  gettimeofday(&first, NULL);
  now = last = first;
  Fl::add_timeout(refms, _update, this);
  startms = 0;
  started = true;
}


void
Regame::reset()
{
  stop();
  redraw();
  particles.clear();
  started = false;
  grabbed = false;
  startms = 0;
  lives = startLives;
  pts = 0;
  mms = data.mms;
  mmd = data.mmd;
  data.player.x = data.w / 2;
  data.player.sx = 0;
  oldDir = 0;
  toNext = 0;
  score = 0;
  for(size_t i = 0; i != data.cnts.size(); ++i)
    data.cnts[i].shakeStart = -data.shakeLen - 1;
}


void
Regame::gameover()
{
  score = startms / 1000 + pts * 100;

  // some fun
  mms = mmd = 100;
  toNext = 0;

  // give the user some time to scream
  Fl::add_timeout(popupTime, _popup, this);
}


void
Regame::gl_draw_cx(const char* str, const int y)
{
  gl_draw(str, w() / 2 - fl_width(str) / 2, y);
}


void
Regame::gl_sprite(const Sprite& s, const Point2f& p, const float a)
{
  glColor4f(1, 1, 1, a);
  gl_sprite2(s, p);
}


void
Regame::gl_sprite2(const Sprite& s, const Point2f& p)
{
  glEnable(target);
  glBindTexture(target, s.tex);
  glBegin(GL_QUADS);
  glTexCoord2f(0   , s.rh); glVertex2f(p.x      , p.y      );
  glTexCoord2f(s.rw, s.rh); glVertex2f(p.x + s.w, p.y      );
  glTexCoord2f(s.rw, 0   ); glVertex2f(p.x + s.w, p.y + s.h);
  glTexCoord2f(0   , 0   ); glVertex2f(p.x      , p.y + s.h);
  glEnd();
  glDisable(target);
}


void
Regame::_update(void* data)
{
  Fl::repeat_timeout(refms, _update, data);
  (reinterpret_cast<Regame*>(data))->update();
}


void
Regame::_popup(void* data)
{
  Regame* rg = reinterpret_cast<Regame*>(data);
  rg->scoreWin.show(rg->score, rg->data.title.c_str());
}


void
Regame::update()
{
  gettimeofday(&now, NULL);
  int delta = tvdiff(now, last);
  if(!delta) return;
  startms = tvdiff(now, first);
  last = now;
  redraw();

  int k = kpLR(key);
  if(k == FL_Left)
  {
    data.player.sx -= data.playerAccel * delta;
    if(data.player.sx < -data.maxPlayerSpeed)
      data.player.sx = -data.maxPlayerSpeed;
  }
  else if(k == FL_Right)
  {
    data.player.sx += data.playerAccel * delta;
    if(data.player.sx > data.maxPlayerSpeed)
      data.player.sx = data.maxPlayerSpeed;
  }
  else if(data.player.sx)
  {
    float d = copysign(1, data.player.sx) * data.playerAccel * delta;
    if(fabs(d) > fabs(data.player.sx))
      data.player.sx = 0;
    else
      data.player.sx -= d;
  }

  data.player.x += delta * data.player.sx;
  if(data.player.x < 0) { data.player.x = 0; data.player.sx = 0; }
  if(data.player.x > data.w) { data.player.x = data.w; data.player.sx = 0; }

  // new particles
  int immd = static_cast<int>(mmd);
  if(immd > 0 && (toNext -= delta) < 0)
  {
    toNext += mms + rand() % immd;

    Particle buf;
    buf.type = rand() % data.cnts.size();
    buf.x = data.fallWin[0] + rand() % (data.fallWin[1] - data.fallWin[0]);
    buf.y = data.h + data.objs[buf.type].h;
    buf.sx = buf.sy = 0;
    buf.grabbed = false;
    buf.maxSpeed = (rand() + RAND_MAX / 5.) / RAND_MAX * data.maxFallSpeed;
    buf.rand = rand();
    particles.push_back(buf);
  }

  // recalculate positions
  for(vector<Particle>::iterator it = particles.begin();
      it != particles.end();)
  {
    if(!it->grabbed || it->y > data.topline)
    {
      it->grabbed = false;
      it->sy -= data.grav * delta;
      if(it->sy < -it->maxSpeed)
	it->sy = -it->maxSpeed;
    }
    it->y += delta * it->sy;

    if(!it->grabbed && it->y < data.player.y + data.playerAnim[0].h)
    {
      if(!grabbed && it->y > data.player.y && labs(it->x - data.player.x) < data.playerAnim[0].w / 2)
      {
	grabbed = true;
	grabType = it->type;
	particles.erase(it);
	continue;
      }
      else if(it->y < data.baseline)
      {
	if(it->maxSpeed > data.minSpeed)
	{
	  it->y = data.baseline;
	  it->sy = it->maxSpeed;
	  it->maxSpeed /= 2;
	}
	else if(it->y < -data.objs[it->type].h)
	{
	  if(!--lives) gameover();
	  particles.erase(it);
	  continue;
	}
      }
    }

    if(it->grabbed)
    {
      vector<Container>::iterator ct;
      for(ct = data.cnts.begin(); ct != data.cnts.end(); ++ct)
      {
	if(it->type == ct->accept
	&& it->x > ct->pos.x + ct->accWin[0].x
	&& it->x < ct->pos.x + ct->accWin[1].x
	&& it->y > ct->pos.y + ct->accWin[0].y)
	  break;
      }
      if(ct != data.cnts.end())
      {
	++pts;
	particles.erase(it);
	ct->shakeStart = startms;
	continue;
      }
    }

    ++it;
  }

  mms -= delta / 100.;
  mmd -= delta / 1000.;
}


void
Regame::initGL()
{
  string buf;

  // initial settings
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // detect GL_TEXTURE_RECTANGLE_ARB availability
  while(glGetError());
  glEnable(GL_TEXTURE_RECTANGLE_ARB);
  if(glGetError()) target = GL_TEXTURE_2D;
  else glDisable(GL_TEXTURE_RECTANGLE_ARB);

  // background
  loadTex2(data.back, (dataDir + "/" + data.backPrefix + ".png").c_str(), false);

  // player
  for(size_t i = 0; i != data.playerAnim.size(); ++i)
  {
    buf = dataDir;
    buf += "/";
    buf += data.playerPrefix;
    buf += '0' + i;
    buf += ".png";
    loadTex2(data.playerAnim[i], buf.c_str(), true);
  }

  // containers
  for(size_t i = 0; i != data.cnts.size(); ++i)
  {
    buf = dataDir;
    buf += "/";
    buf += data.cntsPrefix;
    buf += '0' + i;
    buf += ".png";
    loadTex2(data.cnts[i].s, buf.c_str(), true);
  }

  // objects
  for(size_t i = 0; i != data.objs.size(); ++i)
  {
    buf = dataDir;
    buf += "/";
    buf += data.objsPrefix;
    buf += '0' + i;
    buf += ".png";
    loadTex2(data.objs[i], buf.c_str(), true);
  }
}


void
Regame::draw()
{
  if(!valid())
  {
    if(!context_valid())
    {
#ifdef EXTENDED_FLTK
      vsync(1);
#endif
      initGL();
    }

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    ortho();
  }

  // background
  gl_sprite(data.back, Point2f(0, 0));

  gl_font(font, fontSize);
  char buf[64];

  // scores
  if(started)
  {
    glColor3fv(data.color);
    int y = data.h;
#if 0
    sprintf(buf, "ms: %d", startms);
    gl_draw(buf, fontSpc, y -= fontSize);
    sprintf(buf, "mms: %.f", mms);
    gl_draw(buf, fontSpc, y -= fontSize);
    sprintf(buf, "mmd: %.f", mmd);
    gl_draw(buf, fontSpc, y -= fontSize);
    sprintf(buf, "pts: %d", pts);
    gl_draw(buf, fontSpc, y -= fontSize);
#endif
    sprintf(buf, "LIVES: %d", lives);
    gl_draw(buf, fontSpc, y -= fontSize);
  }

  // containers
  for(size_t i = 0; i != data.cnts.size(); ++i)
  {
    if(data.cnts[i].shakeStart < startms
    && data.cnts[i].shakeStart + data.shakeLen < startms)
      gl_sprite(data.cnts[i].s, data.cnts[i].pos);
    else
      gl_sprite(data.cnts[i].s, Point2f(
	      data.cnts[i].pos.x + rand() % data.shake - data.shake / 2,
	      data.cnts[i].pos.y + rand() % data.shake - data.shake / 2));
  }

  // player
  int playerFrame = (!data.player.sx? 0:
      static_cast<int>(startms / data.playerFpms)
		   % data.playerAnim.size());

  if(!oldDir || data.player.sx)
    oldDir = (data.player.sx >= 0? 1: 2);

  glPushMatrix();
  glTranslated(data.player.x, data.player.y, 0);
  if(oldDir == 2) glScaled(-1, 1, 1);

  glPushMatrix();
  glScaled(1, -0.3, 1);
  glColor4f(0, 0, 0, 0.3);
  gl_sprite2(data.playerAnim[playerFrame],
      Point2f(-data.playerAnim[playerFrame].w / 2, 0));
  glPopMatrix();

  gl_sprite(data.playerAnim[playerFrame],
      Point2f(-data.playerAnim[playerFrame].w / 2, 0));
  glPopMatrix();

  // grabbed particle
  if(grabbed)
  {
    glPushMatrix();
    glTranslated(data.player.x - data.playerAnim[playerFrame].w / 2,
	data.player.y + data.playerAnim[playerFrame].h - data.objs[grabType].h / 2, 0);
    glScaled(0.5, 0.5, 0);
    gl_sprite(data.objs[grabType], Point2f(0, 0));
    glPopMatrix();
  }

  // particles
  for(vector<Particle>::const_iterator it = particles.begin();
      it != particles.end(); ++it)
  {
    float a = (it->grabbed || (it->y < data.baseline)? 0.5: 1);
    double r = it->rand + startms / (100. +
	(static_cast<double>(it->rand) / RAND_MAX * 160. - 90.));
    r = fmod(r, 360.);
    if(it->rand % 2) r = -r;

    glPushMatrix();
    glTranslated(it->x, it->y, 0);
    glRotated(r, 0, 0, 1);

    gl_sprite(data.objs[it->type], Point2f(
	    -data.objs[it->type].w / 2,
	    -data.objs[it->type].h / 2), a);

    glPopMatrix();
  }

  // other text
  if(lives <= 0)
  {
    int y = data.h / 1.1;
    glColor3fv(data.color);
    gl_draw_cx("GAME OVER", y -= fontSize);
    sprintf(buf, "YOUR SCORE: %d", score);
    gl_draw_cx(buf, y -= fontSize);
    gl_draw_cx("- ESC to reset -", y -= fontSize);
  }
  else if(!started)
  {
    int y = data.h / 1.5;
    glColor3fv(data.color);
    gl_draw_cx(data.title.c_str(), y -= fontSize);
    gl_draw_cx("- space to start -", y -= fontSize);
  }
}


int
Regame::handle(int ev)
{
  if(ev != FL_KEYDOWN && ev != FL_KEYUP)
    return Fl_Gl_Window::handle(ev);

  if(ev == FL_KEYUP)
  {
    if(key == Fl::event_key())
      key = 0;
  }
  else
  {
    switch(Fl::event_key())
    {
    case ' ':
      if(!started)
	start();
      else if(grabbed)
      {
	grabbed = false;

	// reinject the particle
	Particle buf(data.player.x,
	    data.player.y + data.playerAnim[0].h / 2,
	    0, data.maxFallSpeed);
	buf.type = grabType;
	buf.grabbed = true;
	buf.maxSpeed = data.maxFallSpeed / 2;
	buf.rand = rand();
	particles.push_back(buf);
      }
      break;

    case FL_Escape:
      if(started) reset();
      else return Fl_Gl_Window::handle(ev);
      break;

    default:
      if(kpLR(Fl::event_key()))
	key = Fl::event_key();
      break;
    }
  }

  return 1;
}


int
main(int argc, char* argv[])
{
  // search for game data
  const char* dataDir = ".";
  string buf = string(dataDir) + "/" + gameData;
  if(access(buf.c_str(), R_OK))
  {
    buf = dataDir = getResDir();
    buf += "/";
    buf += gameData;
  }

  string_map sm;
  if(loadPairs(sm, buf.c_str()))
  {
    fprintf(stderr, "%s: cannot load game data from %s\n", argv[0], buf.c_str());
    return EXIT_FAILURE;
  }

  srand(time(NULL));

  // run through levels; but no concept of EndGame yet...
  for(int i = 0;; ++i)
  {
    buf = "level";
    buf += '0' + i;
    string_map::const_iterator st = sm.find(buf);
    if(st == sm.end()) break;
    buf = string(dataDir) + "/" + st->second;

    Level data;
    if(loadLevel(data, buf.c_str()))
    {
      fprintf(stderr, "%s: cannot load level %d from %s\n", argv[0], i, buf.c_str());
      return EXIT_FAILURE;
    }

    Regame* game = new Regame(dataDir, &data);
    game->show();
    Fl::run();
    delete game;
  }

  return EXIT_SUCCESS;
}



/*
 * Network
 */

bool
checkName(const char* name)
{
  int n = 0;
  for(const char* p = name; *p; ++p)
    if(!isspace(*p)) ++n;
  return (n < 3);
}


void
encodeString(string& final, const char* s)
{
  for(const char* p = s; *p; ++p)
  {
    char chr[3];
    sprintf(chr, "%02x", *p);
    final += chr;
  }
}


void
submitScore(int score, const char* level, const char* name)
{
  // network scores without sockets? you bet...
  // just cloak the strings a bit to prevent easy cheating

  char buf[32];
  snprintf(buf, sizeof(buf), "%d", score);

  string final;
  encodeString(final, buf);
  final += "00";
  encodeString(final, level);
  final += "00";
  encodeString(final, name);
  final += "00";

  // and some final parity
  char par = 0;
  for(string::const_iterator p = final.begin(); p != final.end(); ++p)
    par ^= *p;
  snprintf(buf, sizeof(buf), "%02x", par);
  final += buf;

  fl_open_uri((string(scoreUrl) + final).c_str(), NULL, 0);
}
