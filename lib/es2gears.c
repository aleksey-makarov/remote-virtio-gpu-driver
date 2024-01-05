/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Ported to GLES2.
 * Kristian Høgsberg <krh@bitplanet.net>
 * May 3, 2010
 *
 * Improve GLES2 port:
 *   * Refactor gear drawing.
 *   * Use correct normals for surfaces.
 *   * Improve shader.
 *   * Use perspective projection transformation.
 *   * Add FPS count.
 *   * Add comments.
 * Alexandros Frantzis <alexandros.frantzis@linaro.org>
 * Jul 13, 2010
 */

#define GL_GLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES

#define _GNU_SOURCE

#include "es2gears.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <epoxy/gl.h>

#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 46
#define GEAR_VERTEX_STRIDE 6

/* Each vertex consist of GEAR_VERTEX_STRIDE GLfloat attributes */
typedef GLfloat GearVertex[GEAR_VERTEX_STRIDE];

/**
 * Struct representing a gear.
 */
struct gear {
   /** The array of vertices comprising the gear */
   GearVertex *vertices;
   /** The number of vertices comprising the gear */
   int nvertices;
   /** The Vertex Buffer Object holding the vertices in the graphics card */
   GLuint vbo;
};

struct es2gears_state {
   /** The view rotation [x, y, z] */
   GLfloat view_rot[3];
   /** The gears */
   struct gear *gear1, *gear2, *gear3;
   /** The current gear rotation angle */
   GLfloat angle;
   /** The location of the shader uniforms */
   GLuint ModelViewProjectionMatrix_location,
               NormalMatrix_location,
               LightSourcePosition_location,
               MaterialColor_location;
   /** The projection matrix */
   GLfloat ProjectionMatrix[16];
};

/** The direction of the directional light for the scene */
static const GLfloat LightSourcePosition[4] = { 5.0, 5.0, 10.0, 1.0};

/**
 * Fills a gear vertex.
 *
 * @param v the vertex to fill
 * @param x the x coordinate
 * @param y the y coordinate
 * @param z the z coortinate
 * @param n pointer to the normal table
 *
 * @return the operation error code
 */
static GearVertex *
vert(GearVertex *v, GLfloat x, GLfloat y, GLfloat z, GLfloat n[3])
{
   v[0][0] = x;
   v[0][1] = y;
   v[0][2] = z;
   v[0][3] = n[0];
   v[0][4] = n[1];
   v[0][5] = n[2];

   return v + 1;
}

/**
 *  Create a gear wheel.
 *
 *  @param inner_radius radius of hole at center
 *  @param outer_radius radius at center of teeth
 *  @param width width of gear
 *  @param teeth number of teeth
 *  @param tooth_depth depth of tooth
 *
 *  @return pointer to the constructed struct gear
 */
static struct gear *
create_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
      GLint teeth, GLfloat tooth_depth)
{
   GLfloat r0, r1, r2;
   GLfloat da;
   GearVertex *v;
   struct gear *gear;
   double s[5], c[5];
   GLfloat normal[3];
   int cur_strip_start = 0;
   int i;

   /* Allocate memory for the gear */
   gear = malloc(sizeof *gear);
   if (gear == NULL)
      return NULL;

   /* Calculate the radii used in the gear */
   r0 = inner_radius;
   r1 = outer_radius - tooth_depth / 2.0;
   r2 = outer_radius + tooth_depth / 2.0;

   da = 2.0 * M_PI / teeth / 4.0;

   /* the first tooth doesn't need the first strip-restart sequence */
   assert(teeth > 0);
   gear->nvertices = VERTICES_PER_TOOTH +
                     (VERTICES_PER_TOOTH + 2) * (teeth - 1);

   /* Allocate memory for the vertices */
   gear->vertices = calloc(gear->nvertices, sizeof(*gear->vertices));
   v = gear->vertices;

   for (i = 0; i < teeth; i++) {
      /* Calculate needed sin/cos for varius angles */
      sincos(i * 2.0 * M_PI / teeth, &s[0], &c[0]);
      sincos(i * 2.0 * M_PI / teeth + da, &s[1], &c[1]);
      sincos(i * 2.0 * M_PI / teeth + da * 2, &s[2], &c[2]);
      sincos(i * 2.0 * M_PI / teeth + da * 3, &s[3], &c[3]);
      sincos(i * 2.0 * M_PI / teeth + da * 4, &s[4], &c[4]);

      /* A set of macros for making the creation of the gears easier */
#define  GEAR_POINT(r, da) { (r) * c[(da)], (r) * s[(da)] }
#define  SET_NORMAL(x, y, z) do { \
   normal[0] = (x); normal[1] = (y); normal[2] = (z); \
} while(0)

#define  GEAR_VERT(v, point, sign) vert((v), p[(point)].x, p[(point)].y, (sign) * width * 0.5, normal)

#define START_STRIP do { \
   cur_strip_start = (v - gear->vertices); \
   if (cur_strip_start) \
      v += 2; \
} while(0);

/* emit prev last vertex
   emit first vertex */
#define END_STRIP do { \
   if (cur_strip_start) { \
      memcpy(gear->vertices + cur_strip_start, \
             gear->vertices + (cur_strip_start - 1), sizeof(GearVertex)); \
      memcpy(gear->vertices + cur_strip_start + 1, \
             gear->vertices + (cur_strip_start + 2), sizeof(GearVertex)); \
   } \
} while (0)

#define QUAD_WITH_NORMAL(p1, p2) do { \
   SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
   v = GEAR_VERT(v, (p1), -1); \
   v = GEAR_VERT(v, (p1), 1); \
   v = GEAR_VERT(v, (p2), -1); \
   v = GEAR_VERT(v, (p2), 1); \
} while(0)

      struct point {
         GLfloat x;
         GLfloat y;
      };

      /* Create the 7 points (only x,y coords) used to draw a tooth */
      struct point p[7] = {
         GEAR_POINT(r2, 1), // 0
         GEAR_POINT(r2, 2), // 1
         GEAR_POINT(r1, 0), // 2
         GEAR_POINT(r1, 3), // 3
         GEAR_POINT(r0, 0), // 4
         GEAR_POINT(r1, 4), // 5
         GEAR_POINT(r0, 4), // 6
      };

      /* Front face */
      START_STRIP;
      SET_NORMAL(0, 0, 1.0);
      v = GEAR_VERT(v, 0, +1);
      v = GEAR_VERT(v, 1, +1);
      v = GEAR_VERT(v, 2, +1);
      v = GEAR_VERT(v, 3, +1);
      v = GEAR_VERT(v, 4, +1);
      v = GEAR_VERT(v, 5, +1);
      v = GEAR_VERT(v, 6, +1);
      END_STRIP;

      /* Back face */
      START_STRIP;
      SET_NORMAL(0, 0, -1.0);
      v = GEAR_VERT(v, 0, -1);
      v = GEAR_VERT(v, 1, -1);
      v = GEAR_VERT(v, 2, -1);
      v = GEAR_VERT(v, 3, -1);
      v = GEAR_VERT(v, 4, -1);
      v = GEAR_VERT(v, 5, -1);
      v = GEAR_VERT(v, 6, -1);
      END_STRIP;

      /* Outer face */
      START_STRIP;
      QUAD_WITH_NORMAL(0, 2);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(1, 0);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(3, 1);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(5, 3);
      END_STRIP;

      /* Inner face */
      START_STRIP;
      SET_NORMAL(-c[0], -s[0], 0);
      v = GEAR_VERT(v, 4, -1);
      v = GEAR_VERT(v, 4, 1);
      SET_NORMAL(-c[4], -s[4], 0);
      v = GEAR_VERT(v, 6, -1);
      v = GEAR_VERT(v, 6, 1);
      END_STRIP;
   }

   assert(gear->nvertices == (v - gear->vertices));

   /* Store the vertices in a vertex buffer object (VBO) */
   glGenBuffers(1, &gear->vbo);
   glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
   glBufferData(GL_ARRAY_BUFFER, gear->nvertices * sizeof(GearVertex),
         gear->vertices, GL_STATIC_DRAW);

   return gear;
}

static void free_gear(struct gear *gear)
{
	free(gear->vertices);
	free(gear);
}

/**
 * Multiplies two 4x4 matrices.
 *
 * The result is stored in matrix m.
 *
 * @param m the first matrix to multiply
 * @param n the second matrix to multiply
 */
static void
multiply(GLfloat *m, const GLfloat *n)
{
   GLfloat tmp[16];
   const GLfloat *row, *column;
   div_t d;
   int i, j;

   for (i = 0; i < 16; i++) {
      tmp[i] = 0;
      d = div(i, 4);
      row = n + d.quot * 4;
      column = m + d.rem;
      for (j = 0; j < 4; j++)
         tmp[i] += row[j] * column[j * 4];
   }
   memcpy(m, &tmp, sizeof tmp);
}

/**
 * Rotates a 4x4 matrix.
 *
 * @param[in,out] m the matrix to rotate
 * @param angle the angle to rotate
 * @param x the x component of the direction to rotate to
 * @param y the y component of the direction to rotate to
 * @param z the z component of the direction to rotate to
 */
static void
rotate(GLfloat *m, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
   double s, c;

   sincos(angle, &s, &c);
   GLfloat r[16] = {
      x * x * (1 - c) + c,     y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
      x * y * (1 - c) - z * s, y * y * (1 - c) + c,     y * z * (1 - c) + x * s, 0,
      x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c,     0,
      0, 0, 0, 1
   };

   multiply(m, r);
}


/**
 * Translates a 4x4 matrix.
 *
 * @param[in,out] m the matrix to translate
 * @param x the x component of the direction to translate to
 * @param y the y component of the direction to translate to
 * @param z the z component of the direction to translate to
 */
static void
translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
   GLfloat t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  x, y, z, 1 };

   multiply(m, t);
}

/**
 * Creates an identity 4x4 matrix.
 *
 * @param m the matrix make an identity matrix
 */
static void
identity(GLfloat *m)
{
   GLfloat t[16] = {
      1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 1.0,
   };

   memcpy(m, t, sizeof(t));
}

/**
 * Transposes a 4x4 matrix.
 *
 * @param m the matrix to transpose
 */
static void
transpose(GLfloat *m)
{
   GLfloat t[16] = {
      m[0], m[4], m[8],  m[12],
      m[1], m[5], m[9],  m[13],
      m[2], m[6], m[10], m[14],
      m[3], m[7], m[11], m[15]};

   memcpy(m, t, sizeof(t));
}

/**
 * Inverts a 4x4 matrix.
 *
 * This function can currently handle only pure translation-rotation matrices.
 * Read http://www.gamedev.net/community/forums/topic.asp?topic_id=425118
 * for an explanation.
 */
static void
invert(GLfloat *m)
{
   GLfloat t[16];
   identity(t);

   // Extract and invert the translation part 't'. The inverse of a
   // translation matrix can be calculated by negating the translation
   // coordinates.
   t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];

   // Invert the rotation part 'r'. The inverse of a rotation matrix is
   // equal to its transpose.
   m[12] = m[13] = m[14] = 0;
   transpose(m);

   // inv(m) = inv(r) * inv(t)
   multiply(m, t);
}

/**
 * Calculate a frustum projection transformation.
 *
 * @param m the matrix to save the transformation in
 * @param l the left plane distance
 * @param r the right plane distance
 * @param b the bottom plane distance
 * @param t the top plane distance
 * @param n the near plane distance
 * @param f the far plane distance
 */
static void
frustum(GLfloat *m, GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
   GLfloat tmp[16];
   identity(tmp);

   GLfloat deltaX = r - l;
   GLfloat deltaY = t - b;
   GLfloat deltaZ = f - n;

   tmp[0] = (2 * n) / deltaX;
   tmp[5] = (2 * n) / deltaY;
   tmp[8] = (r + l) / deltaX;
   tmp[9] = (t + b) / deltaY;
   tmp[10] = -(f + n) / deltaZ;
   tmp[11] = -1;
   tmp[14] = -(2 * f * n) / deltaZ;
   tmp[15] = 0;

   memcpy(m, tmp, sizeof(tmp));
}

/**
 * Draws a gear.
 *
 * @param gear the gear to draw
 * @param transform the current transformation matrix
 * @param x the x position to draw the gear at
 * @param y the y position to draw the gear at
 * @param angle the rotation angle of the gear
 * @param color the color of the gear
 */
static void
draw_gear(struct es2gears_state * state, struct gear *gear, GLfloat *transform,
      GLfloat x, GLfloat y, GLfloat angle, const GLfloat color[4])
{
   GLfloat model_view[16];
   GLfloat normal_matrix[16];
   GLfloat model_view_projection[16];

   /* Translate and rotate the gear */
   memcpy(model_view, transform, sizeof (model_view));
   translate(model_view, x, y, 0);
   rotate(model_view, 2 * M_PI * angle / 360.0, 0, 0, 1);

   /* Create and set the ModelViewProjectionMatrix */
   memcpy(model_view_projection, state->ProjectionMatrix, sizeof(model_view_projection));
   multiply(model_view_projection, model_view);

   glUniformMatrix4fv(state->ModelViewProjectionMatrix_location, 1, GL_FALSE,
                      model_view_projection);

   /*
    * Create and set the NormalMatrix. It's the inverse transpose of the
    * ModelView matrix.
    */
   memcpy(normal_matrix, model_view, sizeof (normal_matrix));
   invert(normal_matrix);
   transpose(normal_matrix);
   glUniformMatrix4fv(state->NormalMatrix_location, 1, GL_FALSE, normal_matrix);

   /* Set the gear color */
   glUniform4fv(state->MaterialColor_location, 1, color);

   /* Set the vertex buffer object to use */
   glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);

   /* Set up the position of the attributes in the vertex buffer object */
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
         6 * sizeof(GLfloat), NULL);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
         // 6 * sizeof(GLfloat), (GLfloat *) 0 + 3);
         6 * sizeof(GLfloat), (void *)(sizeof(GLfloat) * 3));

   /* Enable the attributes */
   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);

   /* Draw the triangle strips that comprise the gear */
   glDrawArrays(GL_TRIANGLE_STRIP, 0, gear->nvertices);

   /* Disable the attributes */
   glDisableVertexAttribArray(1);
   glDisableVertexAttribArray(0);
}

/**
 * Draws the gears.
 */
void es2gears_draw(struct es2gears_state * state)
{
   static const GLfloat   red[4] = { 0.8, 0.1, 0.0, 1.0 };
   static const GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
   static const GLfloat  blue[4] = { 0.2, 0.2, 1.0, 1.0 };
   GLfloat transform[16];
   identity(transform);

   glClearColor(0.0, 0.0, 0.0, 0.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   /* Translate and rotate the view */
   translate(transform, 0, 0, -40);
   rotate(transform, 2 * M_PI * state->view_rot[0] / 360.0, 1, 0, 0);
   rotate(transform, 2 * M_PI * state->view_rot[1] / 360.0, 0, 1, 0);
   rotate(transform, 2 * M_PI * state->view_rot[2] / 360.0, 0, 0, 1);

   /* Draw the gears */
   draw_gear(state, state->gear1, transform, -3.0, -2.0,      state->angle,        red);
   draw_gear(state, state->gear2, transform,  3.1, -2.0, -2 * state->angle - 9.0,  green);
   draw_gear(state, state->gear3, transform, -3.1,  4.2, -2 * state->angle - 25.0, blue);
}

/**
 * Handles a new window size or exposure.
 *
 * @param width the window width
 * @param height the window height
 */
void es2gears_reshape(struct es2gears_state * state, int width, int height)
{
   /* Update the projection matrix */
   GLfloat h = (GLfloat)height / (GLfloat)width;
   frustum(state->ProjectionMatrix, -1.0, 1.0, -h, h, 5.0, 60.0);

   /* Set the viewport */
   glViewport(0, 0, (GLint) width, (GLint) height);
}

/**
 * Handles special eglut events.
 *
 * @param special the event to handle.
 */
void es2gears_special(struct es2gears_state * state, enum special special)
{
   switch (special) {
      case SPECIAL_LEFT:
         state->view_rot[1] += 5.0;
         break;
      case SPECIAL_RIGHT:
         state->view_rot[1] -= 5.0;
         break;
      case SPECIAL_UP:
         state->view_rot[0] += 5.0;
         break;
      case SPECIAL_DOWN:
         state->view_rot[0] -= 5.0;
         break;
   }
}

void es2gears_idle(struct es2gears_state * state, unsigned long int elapsed_time_ms)
{
   static int frames = 0;
   static double tRot0 = -1.0, tRate0 = -1.0;
   double dt, t = elapsed_time_ms / 1000.0;

   if (tRot0 < 0.0)
      tRot0 = t;
   dt = t - tRot0;
   tRot0 = t;

   /* advance rotation for next frame */
   state->angle += 70.0 * dt;  /* 70 degrees per second */
   if (state->angle > 3600.0)
      state->angle -= 3600.0;

   // eglutPostRedisplay();
   frames++;

   if (tRate0 < 0.0)
      tRate0 = t;
   if (t - tRate0 >= 5.0) {
      GLfloat seconds = t - tRate0;
      GLfloat fps = frames / seconds;
      printf("%d frames in %3.1f seconds = %6.3f FPS\n", frames, seconds,
            fps);
      tRate0 = t;
      frames = 0;
   }
}

static const char vertex_shader[] =
"attribute vec3 position;\n"
"attribute vec3 normal;\n"
"\n"
"uniform mat4 ModelViewProjectionMatrix;\n"
"uniform mat4 NormalMatrix;\n"
"uniform vec4 LightSourcePosition;\n"
"uniform vec4 MaterialColor;\n"
"\n"
"varying vec4 Color;\n"
"\n"
"void main(void)\n"
"{\n"
"    // Transform the normal to eye coordinates\n"
"    vec3 N = normalize(vec3(NormalMatrix * vec4(normal, 1.0)));\n"
"\n"
"    // The LightSourcePosition is actually its direction for directional light\n"
"    vec3 L = normalize(LightSourcePosition.xyz);\n"
"\n"
"    float diffuse = max(dot(N, L), 0.0);\n"
"    float ambient = 0.2;\n"
"\n"
"    // Multiply the diffuse value by the vertex color (which is fixed in this case)\n"
"    // to get the actual color that we will use to draw this vertex with\n"
"    Color = vec4((ambient + diffuse) * MaterialColor.xyz, MaterialColor.a);\n"
"\n"
"    // Transform the position to clip coordinates\n"
"    gl_Position = ModelViewProjectionMatrix * vec4(position, 1.0);\n"
"}";

static const char fragment_shader[] =
"precision mediump float;\n"
"varying vec4 Color;\n"
"\n"
"void main(void)\n"
"{\n"
"    gl_FragColor = Color;\n"
"}";

struct es2gears_state *es2gears_init(void)
{
   GLuint v, f, program;
   const char *p;
   // char msg[512];

   struct es2gears_state *ret;

   ret = calloc(1, sizeof(struct es2gears_state));
   if (!ret)
      goto error;

   ret->view_rot[0] = 20.0;
   ret->view_rot[1] = 30.0;
   ret->view_rot[2] =  0.0;

   glEnable(GL_CULL_FACE);
   glEnable(GL_DEPTH_TEST);

   /* Compile the vertex shader */
   p = vertex_shader;
   v = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(v, 1, &p, NULL);
   glCompileShader(v);
   // glGetShaderInfoLog(v, sizeof msg, NULL, msg);
   // printf("vertex shader info: %s\n", msg);

   /* Compile the fragment shader */
   p = fragment_shader;
   f = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(f, 1, &p, NULL);
   glCompileShader(f);
   // glGetShaderInfoLog(f, sizeof msg, NULL, msg);
   // printf("fragment shader info: %s\n", msg);

   /* Create and link the shader program */
   program = glCreateProgram();
   glAttachShader(program, v);
   glAttachShader(program, f);
   glBindAttribLocation(program, 0, "position");
   glBindAttribLocation(program, 1, "normal");

   glLinkProgram(program);
   // glGetProgramInfoLog(program, sizeof msg, NULL, msg);
   // printf("info: %s\n", msg);

   /* Enable the shaders */
   glUseProgram(program);

   /* Get the locations of the uniforms so we can access them */
   ret->ModelViewProjectionMatrix_location = glGetUniformLocation(program, "ModelViewProjectionMatrix");
   ret->NormalMatrix_location = glGetUniformLocation(program, "NormalMatrix");
   ret->LightSourcePosition_location = glGetUniformLocation(program, "LightSourcePosition");
   ret->MaterialColor_location = glGetUniformLocation(program, "MaterialColor");

   /* Set the LightSourcePosition uniform which is constant throught the program */
   glUniform4fv(ret->LightSourcePosition_location, 1, LightSourcePosition);

   /* make the gears */
   ret->gear1 = create_gear(1.0, 4.0, 1.0, 20, 0.7);
   ret->gear2 = create_gear(0.5, 2.0, 2.0, 10, 0.7);
   ret->gear3 = create_gear(1.3, 2.0, 0.5, 10, 0.7);

   return ret;

error:
   return 0;
}

void es2gears_done(struct es2gears_state *state)
{
   free_gear(state->gear1);
   free_gear(state->gear2);
   free_gear(state->gear3);
}