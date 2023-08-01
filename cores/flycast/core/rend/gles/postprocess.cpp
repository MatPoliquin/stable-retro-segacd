/*
	PowerVR2 buffer shader
    Authors: leilei

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version.
*/
#include <array>
#include "gles.h"

extern int screen_width;
extern int screen_height;

PostProcessor postProcessor;

static const char* VertexShaderSource = R"(%s
#define TARGET_GL %s

#define GLES2 0
#define GLES3 1
#define GL2 2
#define GL3 3

#if TARGET_GL == GL3 || TARGET_GL == GLES3
#define COMPAT_VARYING in
#else
#define COMPAT_VARYING attribute
#endif

COMPAT_VARYING vec3 in_pos;

void main()
{
	gl_Position = vec4(in_pos, 1.0);
}
)";

static const char* FragmentShaderSource = R"(%s
#define TARGET_GL %s
#define DITHERING %d
#define INTERLACED %d
#define VGASIGNAL %d
#define LUMBOOST 0

#define GLES2 0
#define GLES3 1
#define GL2 2
#define GL3 3

#if TARGET_GL == GLES2 || TARGET_GL == GLES3
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

#if TARGET_GL == GL3 || TARGET_GL == GLES3
#define COMPAT_TEXTURE texture
out vec4 FragColor;
#else
#define FragColor gl_FragColor
#define COMPAT_TEXTURE texture2D
#endif

uniform int FrameCount;
uniform sampler2D Texture;

// compatibility #defines
#define Source Texture
#define TextureSize textureSize(Texture, 0)
#define vTexCoord (gl_FragCoord.xy / vec2(textureSize(Texture, 0)))
#define texture(c, d) COMPAT_TEXTURE(c, d)

float dithertable[16] = float[](
	16.,4.,13.,1.,   
	8.,12.,5.,9.,
	14.,2.,15.,3.,
	6.,10.,7.,11.		
);

//#pragma parameter INTERLACED "PVR - Interlace smoothing" 1.00 0.00 1.00 1.0
//#pragma parameter VGASIGNAL "PVR - VGA signal loss" 0.00 0.00 1.00 1.0
//#pragma parameter LUMBOOST "PVR - Luminance gain" 0.35 0.00 1.00 0.01

#define LUM_R (76.0/255.0)
#define LUM_G (150.0/255.0)
#define LUM_B (28.0/255.0)

void main()
{
	vec2 texcoord = vTexCoord;
	vec2 texcoord2 = vTexCoord;
	texcoord2.x *= float(TextureSize.x);
	texcoord2.y *= float(TextureSize.y);
	vec4 color = COMPAT_TEXTURE(Source, texcoord);
	float fc = mod(float(FrameCount), 2.0);

#if INTERLACED == 1
	// Blend vertically for composite mode
	int taps = int(3);
	float tap = (2.666f/float(taps)) / float(min(TextureSize.y, 720));
	vec2 texcoord4  = vTexCoord;
	texcoord4.y -= tap * 2.f;
	int bl;
	vec4 ble;

	for (bl=0;bl<taps;bl++)
	{
		texcoord4.y += tap;
		ble.rgb += (COMPAT_TEXTURE(Source, texcoord4).rgb / float(taps+1));
	}
	color.rgb = (color.rgb / float(taps+1)) + ( ble.rgb );
#endif

#if LUMBOOST == 1
	// Some games use a luminance boost (JSR etc)
	color.rgb += (((color.r * LUM_R) + (color.g * LUM_G) + (color.b * LUM_B)) * LUMBOOST);
#endif

#if DITHERING == 1
	// Dither
	int ditdex = 	int(mod(texcoord2.x, 4.0)) * 4 + int(mod(texcoord2.y, 4.0)); 	
	int yeh = 0;
	float ohyes;
	vec4 how;

	for (yeh=ditdex; yeh<(ditdex+16); yeh++) 	ohyes =  ((((dithertable[yeh-15]) - 1.f) * 0.1));
	color.rb -= (ohyes / 128.);
	color.g -= (ohyes / 128.);
	{
		vec4 reduct;		// 16 bits per pixel (5-6-5)
		reduct.r = 32.;
		reduct.g = 64.;	
		reduct.b = 32.;
		how = color;
  		how = pow(how, vec4(1.0, 1.0, 1.0, 1.0));  	how *= reduct;  	how = floor(how);	how = how / reduct;  	how = pow(how, vec4(1.0, 1.0, 1.0, 1.0));
	}

	color.rb = how.rb;
	color.g = how.g;
#endif

#if VGASIGNAL == 1
	// There's a bit of a precision drop involved in the RGB565ening for VGA
	// I'm not sure why that is. it's exhibited on PVR1 and PVR3 hardware too
	if (mod(color.r*32, 2.0)>0) color.r -= 0.023;
	if (mod(color.g*64, 2.0)>0) color.g -= 0.01;
	if (mod(color.b*32, 2.0)>0) color.b -= 0.023;
#endif

	// RGB565 clamp

	color.rb = floor(color.rb * 32. + 0.5)/32.;
	color.g = floor(color.g * 64. + 0.5)/64.;

#if VGASIGNAL == 1
	// VGA Signal Loss, which probably is very wrong but i tried my best
	int taps = 32;
	float tap = 12.0/taps;
	vec2 texcoord4  = vTexCoord;
	texcoord4.x = texcoord4.x + (2.0/640.0);
	texcoord4.y = texcoord4.y;
	vec4 blur1 = COMPAT_TEXTURE(Source, texcoord4);
	int bl;
	vec4 ble;
	for (bl=0;bl<taps;bl++)
	{
		float e = 1;
		if (bl>=3)
		e=0.35;
		texcoord4.x -= (tap  / 640);
		ble.rgb += (COMPAT_TEXTURE(Source, texcoord4).rgb * e) / (taps/(bl+1));
	}

	color.rgb += ble.rgb * 0.015;

	//color.rb += (4.0/255.0);
	color.g += (9.0/255.0);
#endif

	FragColor = vec4(color);
} 
)";

class PostProcessShader
{
public:
	static void select(bool dither, bool interlaced, bool vga)
	{
		u32 key = ((int)dither << 2) | ((int)interlaced << 1) | (int)vga;
		if (shaders[key].program == 0)
			shaders[key].compile(dither, interlaced, vga);
		shaders[key].select();
	}
	static void term()
	{
		for (auto& shader : shaders)
		{
			if (shader.program != 0)
			{
				glDeleteProgram(shader.program);
				shader.program = 0;
			}
		}
	}

private:
	void compile(bool dither, bool interlaced, bool vga)
	{
		char vshader[16384];
		sprintf(vshader, VertexShaderSource, gl.glsl_version_header, gl.gl_version);

		char pshader[16384];
		sprintf(pshader, FragmentShaderSource, gl.glsl_version_header, gl.gl_version, (int)dither, (int)interlaced, (int)vga);

		program = gl_CompileAndLink(vshader, pshader);

		//setup texture 0 as the input for the shader
		GLint gu = glGetUniformLocation(program, "Texture");
		if (gu != -1)
			glUniform1i(gu, 0);

		frameCountUniform = glGetUniformLocation(program, "FrameCount");
	}

	void select()
	{
		glcache.UseProgram(program);
		glUniform1f(frameCountUniform, FrameCount);
	}

	GLuint program = 0;
	GLint frameCountUniform = 0;
	static std::array<PostProcessShader, 8> shaders;
};

std::array<PostProcessShader, 8> PostProcessShader::shaders;

void PostProcessor::Init()
{
	this->width = screen_width;
	this->height = screen_height;

	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	texture = glcache.GenTexture();
	glcache.BindTexture(GL_TEXTURE_2D, texture);
	glcache.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glcache.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

	glGenRenderbuffers(1, &depthBuffer);
	glBindRenderbuffer(RARCH_GL_RENDERBUFFER, depthBuffer);
	glRenderbufferStorage(RARCH_GL_RENDERBUFFER, RARCH_GL_DEPTH24_STENCIL8, width, height);

#if defined(HAVE_OPENGLES2) || defined(HAVE_OPENGLES1) || defined(OSX_PPC)
	glFramebufferRenderbuffer(RARCH_GL_FRAMEBUFFER, RARCH_GL_DEPTH_ATTACHMENT,
         RARCH_GL_RENDERBUFFER, depthBuffer);
   glFramebufferRenderbuffer(RARCH_GL_FRAMEBUFFER, RARCH_GL_STENCIL_ATTACHMENT,
         RARCH_GL_RENDERBUFFER, depthBuffer);
#else
   glFramebufferRenderbuffer(RARCH_GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
         RARCH_GL_RENDERBUFFER, depthBuffer);
#endif
	GLuint uStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	verify(uStatus == GL_FRAMEBUFFER_COMPLETE);
	glcache.BindTexture(GL_TEXTURE_2D, 0);

	float vertices[] = {
			-1,  1, 1,
			-1, -1, 1,
			 1,  1, 1,
			 1, -1, 1,
	};
	glGenBuffers(1, &vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
#ifndef HAVE_OPENGLES
	if (settings.pvr.rend == 3)
	{
		glGenVertexArrays(1, &vertexArray);
		glBindVertexArray(vertexArray);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);
		glDisableVertexAttribArray(3);
	}
#endif
	glCheck();
}

void PostProcessor::Term()
{
	glcache.DeleteTextures(1, &texture);
	texture = 0;
	glDeleteFramebuffers(1, &framebuffer);
	framebuffer = 0;
	glDeleteRenderbuffers(1, &depthBuffer);
	depthBuffer = 0;
	glDeleteBuffers(1, &vertexBuffer);
	vertexBuffer = 0;
#ifndef HAVE_OPENGLES
	if (vertexArray != 0)
		glDeleteVertexArrays(1, &vertexArray);
#endif
	vertexArray = 0;
	PostProcessShader::term();
	glCheck();
}

void PostProcessor::SelectFramebuffer()
{
	if (framebuffer == 0)
		Init();
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

void PostProcessor::Render(GLuint output_fbo)
{
	glcache.Disable(GL_SCISSOR_TEST);
	glcache.Disable(GL_DEPTH_TEST);
	glcache.Disable(GL_STENCIL_TEST);
	glcache.Disable(GL_CULL_FACE);
	glcache.Disable(GL_BLEND);

	PostProcessShader::select(FB_W_CTRL.fb_dither, SPG_CONTROL.interlace, FB_R_CTRL.vclk_div == 1 && SPG_CONTROL.interlace == 0);
#ifndef HAVE_OPENGLES
	if (vertexArray != 0)
		glBindVertexArray(vertexArray);
	else
#endif
	{
		glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);
		glDisableVertexAttribArray(3);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
	glActiveTexture(GL_TEXTURE0);
	glcache.BindTexture(GL_TEXTURE_2D, texture);

   glcache.ClearColor(0.f, 0.f, 0.f, 0.f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
