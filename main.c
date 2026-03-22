#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/Keyboard.h>

#define PROGRESS_BAR_WIDTH 1000
#define PROGRESS_BAR_HEIGHT 35
#define OSCILLOSCOPE_WIDTH 1000
#define OSCILLOSCOPE_HEIGHT 100
#define AUDIO_BUFFER_SIZE 2048
#define AUDIO_BUFFER_COUNT 2
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUFFER_SAMPLES 4096


int PS4_Keyboard_Init (void);
int PS4_Keyboard_Open (void);
int PS4_Keyboard_PumpEvents (void);
int PS4_Keyboard_Close (void);

float audioBuffer[AUDIO_BUFFER_SIZE];
int bufferIndex = 0;
int audioDataAvailable = 0;

typedef struct
{
  OggVorbis_File *vf;
  int current_section;
  int eof;
  int loop;
} AudioData;

void
audioCallback (void *userdata, Uint8 * stream, int len)
{
  AudioData *audio = (AudioData *) userdata;
  int samples_needed = len / sizeof (Sint16);
  Sint16 *buffer = (Sint16 *) stream;
  int samples_read = 0;

  while (samples_read < samples_needed)
    {
      if (audio->eof)
	{
	  if (audio->loop)
	    {

	      ov_time_seek (audio->vf, 0);
	      audio->eof = 0;
	      audio->current_section = 0;
	    }
	  else
	    {
	      break;
	    }
	}

      long ret = ov_read (audio->vf,
			  (char *) (buffer + samples_read),
			  (samples_needed - samples_read) * sizeof (Sint16),
			  0,	// little endian
			  2,	// 2 bytes per sample (16-bit)
			  1,	// signed
			  &audio->current_section);

      if (ret == 0)
	{

	  audio->eof = 1;
	  continue;
	}
      else if (ret < 0)
	{

	  printf ("Error reading audio: %ld\n", ret);
	  break;
	}
      else
	{
	  int samples = ret / sizeof (Sint16);

	  for (int i = 0; i < samples; i++)
	    {
	      audioBuffer[bufferIndex] = buffer[samples_read + i] / 32768.0f;
	      bufferIndex = (bufferIndex + 1) % AUDIO_BUFFER_SIZE;
	    }

	  samples_read += samples;
	}
    }

  if (samples_read < samples_needed)
    {
      memset (buffer + samples_read, 0,
	      (samples_needed - samples_read) * sizeof (Sint16));
    }

  audioDataAvailable = 1;
}

void
drawProgressBar (SDL_Renderer * renderer, float progress, int windowWidth,
		 int windowHeight)
{
  int x = (windowWidth - PROGRESS_BAR_WIDTH) / 2;
  int y = 20;

  if (progress < 0)
    progress = 0;
  if (progress > 1)
    progress = 1;

  int barWidth = (int) (progress * PROGRESS_BAR_WIDTH);
  if (barWidth < 0)
    barWidth = 0;

  SDL_Rect progressBarRect = { x, y, barWidth, PROGRESS_BAR_HEIGHT };
  SDL_SetRenderDrawColor (renderer, 255, 0, 0, 255);
  SDL_RenderFillRect (renderer, &progressBarRect);

  SDL_Rect outlineRect = { x, y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT };
  SDL_SetRenderDrawColor (renderer, 255, 255, 255, 255);
  SDL_RenderDrawRect (renderer, &outlineRect);
}

void
drawOscilloscope (SDL_Renderer * renderer, int windowWidth, int windowHeight)
{
  if (!audioDataAvailable)
    return;

  SDL_SetRenderDrawColor (renderer, 0, 255, 0, 255);

  int startX = (windowWidth - OSCILLOSCOPE_WIDTH) / 2;
  int startY = (windowHeight - OSCILLOSCOPE_HEIGHT) / 2;
  int oscilloscopeHeight = OSCILLOSCOPE_HEIGHT;

  for (int i = 0; i < OSCILLOSCOPE_WIDTH - 1; ++i)
    {
      int sampleIndex1 = (bufferIndex + i) % AUDIO_BUFFER_SIZE;
      int sampleIndex2 = (bufferIndex + i + 1) % AUDIO_BUFFER_SIZE;

      int y1 =
	startY + (int) (audioBuffer[sampleIndex1] * oscilloscopeHeight / 2) +
	oscilloscopeHeight / 2;
      int y2 =
	startY + (int) (audioBuffer[sampleIndex2] * oscilloscopeHeight / 2) +
	oscilloscopeHeight / 2;

      if (y1 < startY)
	y1 = startY;
      if (y1 > startY + oscilloscopeHeight)
	y1 = startY + oscilloscopeHeight;
      if (y2 < startY)
	y2 = startY;
      if (y2 > startY + oscilloscopeHeight)
	y2 = startY + oscilloscopeHeight;

      SDL_RenderDrawLine (renderer, startX + i, y1, startX + i + 1, y2);
    }
}

double
getOggDuration (const char *filename)
{
  OggVorbis_File vf;
  FILE *file = fopen (filename, "rb");
  if (!file)
    {
      return 0.0;
    }

  if (ov_open (file, &vf, NULL, 0) < 0)
    {
      fclose (file);
      return 0.0;
    }

  double totalDuration = ov_time_total (&vf, -1);
  ov_clear (&vf);
  return totalDuration;
}

int
main (int argc, char *argv[])
{

  const char *filename;

  filename = "/app0/assets/audio/h0ffman-eon.ogg";

  FILE *oggFile = fopen (filename, "rb");
  if (!oggFile)
    {
      printf ("Error: Could not open file: %s\n", filename);
      return 1;
    }

  OggVorbis_File vf;
  if (ov_open (oggFile, &vf, NULL, 0) < 0)
    {
      printf ("Error: File is not a valid OGG Vorbis file: %s\n", filename);
      fclose (oggFile);
      return 1;
    }

  vorbis_info *vi = ov_info (&vf, -1);

  double totalDurationSeconds = ov_time_total (&vf, -1);
  Uint32 totalDurationMs = (Uint32) (totalDurationSeconds * 1000);

  if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
      printf ("Error initializing SDL: %s\n", SDL_GetError ());
      ov_clear (&vf);
      return 1;
    }

  PS4_Keyboard_Init ();
  PS4_Keyboard_Open ();

  SDL_Window *window = SDL_CreateWindow ("ogg-plr",
					 SDL_WINDOWPOS_UNDEFINED,
					 SDL_WINDOWPOS_UNDEFINED,
					 1200, 600,
					 SDL_WINDOW_SHOWN);
  if (window == NULL)
    {
      printf ("Error creating window: %s\n", SDL_GetError ());
      SDL_Quit ();
      ov_clear (&vf);
      return 1;
    }

  SDL_Renderer *renderer =
    SDL_CreateRenderer (window, -1, SDL_RENDERER_SOFTWARE);
  if (renderer == NULL)
    {
      printf ("Error creating renderer: %s\n", SDL_GetError ());
      SDL_DestroyWindow (window);
      SDL_Quit ();
      ov_clear (&vf);
      return 1;
    }

  SDL_AudioSpec want, have;
  SDL_memset (&want, 0, sizeof (want));
  want.freq = vi->rate;
  want.format = AUDIO_S16SYS;
  want.channels = vi->channels;
  want.samples = AUDIO_BUFFER_SAMPLES;
  want.callback = audioCallback;

  AudioData audioData;
  audioData.vf = &vf;
  audioData.current_section = 0;
  audioData.eof = 0;
  audioData.loop = 1;		// Enable loop
  want.userdata = &audioData;

  SDL_AudioDeviceID audioDevice =
    SDL_OpenAudioDevice (NULL, 0, &want, &have, 0);
  if (audioDevice == 0)
    {
      printf ("Error opening audio: %s\n", SDL_GetError ());
      SDL_DestroyRenderer (renderer);
      SDL_DestroyWindow (window);
      SDL_Quit ();
      ov_clear (&vf);
      return 1;
    }

  SDL_PauseAudioDevice (audioDevice, 0);

  Uint32 startTime = SDL_GetTicks ();
  Uint32 elapsedTime = 0;
  int paused = 0;
  int quit = 0;
  SDL_Event event;

  printf ("Player started. Press SPACE to pause/resume, ESC to exit\n");

  while (!quit)
    {
      PS4_Keyboard_PumpEvents ();
      while (SDL_PollEvent (&event))
	{
	  if (event.type == SDL_QUIT)
	    {
	      quit = 1;
	    }
	  if (event.type == SDL_KEYDOWN)
	    {
	      if (event.key.keysym.sym == SDLK_SPACE)
		{
		  if (paused)
		    {
		      SDL_PauseAudioDevice (audioDevice, 0);
		      startTime = SDL_GetTicks () - elapsedTime;
		      paused = 0;
		      printf ("▶ Resumed\n");
		    }
		  else
		    {
		      SDL_PauseAudioDevice (audioDevice, 1);
		      elapsedTime = SDL_GetTicks () - startTime;
		      paused = 1;
		      printf ("⏸ Paused\n");
		    }
		}
	      else if (event.key.keysym.sym == SDLK_ESCAPE)
		{
		  quit = 1;
		  printf ("Exiting...\n");
		}
	    }
	}

      float position;
      if (!paused)
	{
	  elapsedTime = SDL_GetTicks () - startTime;

	  position =
	    (float) (elapsedTime % totalDurationMs) / totalDurationMs;
	}
      else
	{
	  position = (float) elapsedTime / totalDurationMs;
	  if (position > 1.0f)
	    position = 1.0f;
	}

      SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
      SDL_RenderClear (renderer);

      int windowWidth, windowHeight;
      SDL_GetWindowSize (window, &windowWidth, &windowHeight);

      drawProgressBar (renderer, position, windowWidth, windowHeight);
      drawOscilloscope (renderer, windowWidth, windowHeight);

      char title[256];
      int currentLoop = (elapsedTime / totalDurationMs) + 1;
      if (paused)
	{
	  snprintf (title, sizeof (title),
		    "OGGPLR - %.1f%% PAUSED | Loop: ON | SPACE: Pause, ESC: Exit",
		    position * 100);
	}
      else
	{
	  snprintf (title, sizeof (title),
		    "OGGPLR - %.1f%% | Loop: ON | SPACE: Pause, ESC: Exit",
		    position * 100);
	}
      SDL_SetWindowTitle (window, title);

      SDL_RenderPresent (renderer);
      SDL_Delay (16);
    }

  SDL_CloseAudioDevice (audioDevice);
  SDL_DestroyRenderer (renderer);
  SDL_DestroyWindow (window);
  SDL_Quit ();
  ov_clear (&vf);

  return 0;
}
