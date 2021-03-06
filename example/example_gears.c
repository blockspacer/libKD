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

#define EXAMPLE_COMMON_IMPLEMENTATION
#include "example_common.h"

#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 34
#define GEAR_VERTEX_STRIDE 6

/**
 * Struct describing the vertices in triangle strip
 */
struct vertex_strip {
    /** The first vertex in the strip */
    GLint first;
    /** The number of consecutive vertices in the strip after the first */
    GLint count;
};

/* Each vertex consist of GEAR_VERTEX_STRIDE GLfloat attributes */
typedef GLfloat GearVertex[GEAR_VERTEX_STRIDE];

/**
 * Struct representing a gear.
 */
struct gear {
    /** The array of vertices comprising the gear */
    GearVertex *vertices;
    /** The number of vertices comprising the gear */
    KDint nvertices;
    /** The array of triangle strips comprising the gear */
    struct vertex_strip *strips;
    /** The number of triangle strips comprising the gear */
    KDint nstrips;
    /** The Vertex Buffer Object holding the vertices in the graphics card */
    GLuint vbo;
};

/** The view rotation [x, y, z] */
static GLfloat view_rot[3] = {20.0, 30.0, 0.0};
/** The gears */
static struct gear *gear1, *gear2, *gear3;
/** The location of the shader uniforms */
static GLuint ModelViewProjectionMatrix_location,
    NormalMatrix_location,
    LightSourcePosition_location,
    MaterialColor_location;
/** The projection matrix */
static GLfloat ProjectionMatrix[16];
/** The direction of the directional light for the scene */
static const GLfloat LightSourcePosition[4] = {5.0, 5.0, 10.0, 1.0};

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
    KDfloat64KHR s[5], c[5];
    GLfloat normal[3];
    KDint cur_strip = 0;
    KDint i;

    /* Allocate memory for the gear */
    gear = kdMalloc(sizeof *gear);
    if(gear == KD_NULL)
        return KD_NULL;

    /* Calculate the radii used in the gear */
    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;

    da = 2.0f * KD_PI_F / teeth / 4.0f;

    /* Allocate memory for the triangle strip information */
    gear->nstrips = STRIPS_PER_TOOTH * teeth;
    gear->strips = kdMalloc(gear->nstrips * sizeof(*gear->strips));

    /* Allocate memory for the vertices */
    gear->vertices = kdMalloc((VERTICES_PER_TOOTH * teeth) * sizeof(*gear->vertices));
    v = gear->vertices;

    for(i = 0; i < teeth; i++)
    {
        /* Calculate needed sin/cos for varius angles */
        s[0] = kdSinKHR(i * 2.0 * KD_PI_F / teeth);
        c[0] = kdCosKHR(i * 2.0 * KD_PI_F / teeth);
        s[1] = kdSinKHR(i * 2.0 * KD_PI_F / teeth + da);
        c[1] = kdCosKHR(i * 2.0 * KD_PI_F / teeth + da);
        s[2] = kdSinKHR(i * 2.0 * KD_PI_F / teeth + da * 2);
        c[2] = kdCosKHR(i * 2.0 * KD_PI_F / teeth + da * 2);
        s[3] = kdSinKHR(i * 2.0 * KD_PI_F / teeth + da * 3);
        c[3] = kdCosKHR(i * 2.0 * KD_PI_F / teeth + da * 3);
        s[4] = kdSinKHR(i * 2.0 * KD_PI_F / teeth + da * 4);
        c[4] = kdCosKHR(i * 2.0 * KD_PI_F / teeth + da * 4);

/* A set of macros for making the creation of the gears easier */
#define GEAR_POINT(r, da)          \
    {                              \
        (r) * (GLfloat)c[(da)], (r) * (GLfloat)s[(da)] \
    }
#define SET_NORMAL(x, y, z) \
    do                      \
    {                       \
        normal[0] = (x);    \
        normal[1] = (y);    \
        normal[2] = (z);    \
    } while(0)

#define GEAR_VERT(v, point, sign) vert((v), p[(point)].x, p[(point)].y, (sign)*width * 0.5f, normal)

#define START_STRIP                                         \
    do                                                      \
    {                                                       \
        gear->strips[cur_strip].first = (GLint)(v - gear->vertices); \
    } while(0);

#define END_STRIP                                                             \
    do                                                                        \
    {                                                                         \
        KDint _tmp = (KDint)(v - gear->vertices);                                    \
        gear->strips[cur_strip].count = _tmp - gear->strips[cur_strip].first; \
        cur_strip++;                                                          \
    } while(0)

#define QUAD_WITH_NORMAL(p1, p2)                                          \
    do                                                                    \
    {                                                                     \
        SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
        v = GEAR_VERT(v, (p1), -1);                                       \
        v = GEAR_VERT(v, (p1), 1);                                        \
        v = GEAR_VERT(v, (p2), -1);                                       \
        v = GEAR_VERT(v, (p2), 1);                                        \
    } while(0)

        struct point {
            GLfloat x;
            GLfloat y;
        };

        /* Create the 7 points (only x,y coords) used to draw a tooth */
        struct point p[7] = {
            GEAR_POINT(r2, 1),  // 0
            GEAR_POINT(r2, 2),  // 1
            GEAR_POINT(r1, 0),  // 2
            GEAR_POINT(r1, 3),  // 3
            GEAR_POINT(r0, 0),  // 4
            GEAR_POINT(r1, 4),  // 5
            GEAR_POINT(r0, 4),  // 6
        };

        /* Front face */
        START_STRIP;
        SET_NORMAL(0.0f, 0.0f, 1.0f);
        v = GEAR_VERT(v, 0, +1);
        v = GEAR_VERT(v, 1, +1);
        v = GEAR_VERT(v, 2, +1);
        v = GEAR_VERT(v, 3, +1);
        v = GEAR_VERT(v, 4, +1);
        v = GEAR_VERT(v, 5, +1);
        v = GEAR_VERT(v, 6, +1);
        END_STRIP;

        /* Inner face */
        START_STRIP;
        QUAD_WITH_NORMAL(4, 6);
        END_STRIP;

        /* Back face */
        START_STRIP;
        SET_NORMAL(0.0f, 0.0f, -1.0f);
        v = GEAR_VERT(v, 6, -1);
        v = GEAR_VERT(v, 5, -1);
        v = GEAR_VERT(v, 4, -1);
        v = GEAR_VERT(v, 3, -1);
        v = GEAR_VERT(v, 2, -1);
        v = GEAR_VERT(v, 1, -1);
        v = GEAR_VERT(v, 0, -1);
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
    }

    gear->nvertices = (KDint)(v - gear->vertices);

    /* Store the vertices in a vertex buffer object (VBO) */
    glGenBuffers(1, &gear->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
    glBufferData(GL_ARRAY_BUFFER, gear->nvertices * sizeof(GearVertex),
        gear->vertices, GL_STATIC_DRAW);

    return gear;
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
        m[0], m[4], m[8], m[12],
        m[1], m[5], m[9], m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]};

    kdMemcpy(m, t, sizeof(t));
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
    exampleMatrixIdentity(t);

    // Extract and invert the translation part 't'. The inverse of a
    // translation matrix can be calculated by negating the translation
    // coordinates.
    t[12] = -m[12];
    t[13] = -m[13];
    t[14] = -m[14];

    // Invert the rotation part 'r'. The inverse of a rotation matrix is
    // equal to its transpose.
    m[12] = m[13] = m[14] = 0;
    transpose(m);

    // inv(m) = inv(r) * inv(t)
    exampleMatrixMultiply(m, t);
}

/** 
 * Calculate a perspective projection transformation.
 * 
 * @param m the matrix to save the transformation in
 * @param fovy the field of view in the y direction
 * @param aspect the view aspect ratio
 * @param zNear the near clipping plane
 * @param zFar the far clipping plane
 */
void perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar)
{
    GLfloat tmp[16];
    exampleMatrixIdentity(tmp);

    KDfloat64KHR sine, cosine;
    GLfloat radians = fovy / 2 * KD_PI_F / 180;

    GLfloat deltaZ = zFar - zNear;
    sine = kdSinKHR(radians);
    cosine = kdSinKHR(radians);

    if((deltaZ == 0) || (sine == 0) || (aspect == 0))
        return;

    GLfloat cotangent = (GLfloat)(cosine / sine);

    tmp[0] = cotangent / aspect;
    tmp[5] = cotangent;
    tmp[10] = -(zFar + zNear) / deltaZ;
    tmp[11] = -1;
    tmp[14] = -2 * zNear * zFar / deltaZ;
    tmp[15] = 0;

    kdMemcpy(m, tmp, sizeof(tmp));
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
draw_gear(struct gear *gear, GLfloat *transform,
    GLfloat x, GLfloat y, GLfloat angle, const GLfloat color[4])
{
    GLfloat model_view[16];
    GLfloat normal_matrix[16];
    GLfloat model_view_projection[16];

    /* Translate and rotate the gear */
    kdMemcpy(model_view, transform, sizeof(model_view));
    exampleMatrixTranslate(model_view, x, y, 0);
    exampleMatrixRotate(model_view, angle, 0, 0, 1);

    /* Create and set the ModelViewProjectionMatrix */
    kdMemcpy(model_view_projection, ProjectionMatrix, sizeof(model_view_projection));
    exampleMatrixMultiply(model_view_projection, model_view);

    glUniformMatrix4fv(ModelViewProjectionMatrix_location, 1, GL_FALSE,
        model_view_projection);

    /* 
    * Create and set the NormalMatrix. It's the inverse transpose of the
    * ModelView matrix.
    */
    kdMemcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);
    glUniformMatrix4fv(NormalMatrix_location, 1, GL_FALSE, normal_matrix);

    /* Set the gear color */
    glUniform4fv(MaterialColor_location, 1, color);

    /* Set the vertex buffer object to use */
    glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);

    /* Set up the position of the attributes in the vertex buffer object */
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        6 * sizeof(GLfloat), KD_NULL);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
        6 * sizeof(GLfloat), (GLfloat *)0 + 3);

    /* Enable the attributes */
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    /* Draw the triangle strips that comprise the gear */
    KDint n;
    for(n = 0; n < gear->nstrips; n++)
        glDrawArrays(GL_TRIANGLE_STRIP, gear->strips[n].first, gear->strips[n].count);

    /* Disable the attributes */
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

/** 
 * Draws the gears.
 */
static void
gears_draw(GLfloat angle)
{
    const static GLfloat red[4] = {0.8f, 0.1f, 0.0f, 1.0f};
    const static GLfloat green[4] = {0.0f, 0.8f, 0.2f, 1.0f};
    const static GLfloat blue[4] = {0.2f, 0.2f, 1.0f, 1.0f};
    GLfloat transform[16];
    exampleMatrixIdentity(transform);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Translate and rotate the view */
    exampleMatrixTranslate(transform, 0, 0, -20);
    exampleMatrixRotate(transform, view_rot[0], 1, 0, 0);
    exampleMatrixRotate(transform, view_rot[1], 0, 1, 0);
    exampleMatrixRotate(transform, view_rot[2], 0, 0, 1);

    /* Draw the gears */
    draw_gear(gear1, transform, -3.0f, -2.0f, angle, red);
    draw_gear(gear2, transform, 3.1f, -2.0f, -2 * angle - 9.0f, green);
    draw_gear(gear3, transform, -3.1f, 4.2f, -2 * angle - 25.0f, blue);
}

/** 
 * Handles a new window size or exposure.
 * 
 * @param width the window width
 * @param height the window height
 */
static void
gears_reshape(KDint width, KDint height)
{
    /* Update the projection matrix */
    exampleMatrixIdentity(ProjectionMatrix);
    exampleMatrixPerspective(ProjectionMatrix, 60.0, (KDfloat32)width / height, 1.0, 1024.0);

    /* Set the viewport */
    glViewport(0, 0, (GLint)width, (GLint)height);
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
    "    // Multiply the diffuse value by the vertex color (which is fixed in this case)\n"
    "    // to get the actual color that we will use to draw this vertex with\n"
    "    float diffuse = max(dot(N, L), 0.0);\n"
    "    Color = diffuse * MaterialColor;\n"
    "\n"
    "    // Transform the position to clip coordinates\n"
    "    gl_Position = ModelViewProjectionMatrix * vec4(position, 1.0);\n"
    "}";

static const char fragment_shader[] =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH  \n"
    "   precision highp float;          \n"
    "#else                              \n"
    "   precision mediump float;        \n"
    "#endif                             \n"
    "                                   \n"
    "varying vec4 Color;                \n"
    "                                   \n"
    "void main(void)                    \n"
    "{                                  \n"
    "    gl_FragColor = Color;          \n"
    "}";

static void
gears_init(void)
{
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    /* Create and link the shader program */
    GLuint program = exampleCreateProgram(vertex_shader, fragment_shader, KD_FALSE);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "normal");
    glLinkProgram(program);

    /* Enable the shaders */
    glUseProgram(program);

    /* Get the locations of the uniforms so we can access them */
    ModelViewProjectionMatrix_location = glGetUniformLocation(program, "ModelViewProjectionMatrix");
    NormalMatrix_location = glGetUniformLocation(program, "NormalMatrix");
    LightSourcePosition_location = glGetUniformLocation(program, "LightSourcePosition");
    MaterialColor_location = glGetUniformLocation(program, "MaterialColor");

    /* Set the LightSourcePosition uniform which is constant throught the program */
    glUniform4fv(LightSourcePosition_location, 1, LightSourcePosition);

    /* make the gears */
    gear1 = create_gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    gear2 = create_gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    gear3 = create_gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
}

KDint KD_APIENTRY kdMain(KDint argc, const KDchar *const *argv)
{
    Example *example = exampleInit();

    KDust t1 = kdGetTimeUST();
    KDfloat32 deltatime;
    KDfloat32 totaltime = 0.0f;
    KDuint frames = 0;
    GLfloat angle = 0.0f;

    gears_init();
    while(example->run)
    {
        const KDEvent *event = kdWaitEvent(-1);
        if(event)
        {
            switch(event->type)
            {
                case(KD_EVENT_QUIT):
                case(KD_EVENT_WINDOW_CLOSE):
                {
                    example->run = KD_FALSE;
                    break;
                }
                case(KD_EVENT_INPUT_KEY_ATX):
                {
                    KDEventInputKeyATX *keyevent = (KDEventInputKeyATX *)(&event->data);
                    switch(keyevent->keycode) 
                    {
                        case(KD_KEY_LEFT_ATX):
                        { 
                            view_rot[1] += 5.0;
                            break;
                        }
                        case(KD_KEY_RIGHT_ATX):
                        { 
                            view_rot[1] -= 5.0;
                            break;
                        }
                        case(KD_KEY_UP_ATX):
                        {
                            view_rot[0] += 5.0;
                            break;
                        }
                        case(KD_KEY_DOWN_ATX):
                        {
                            view_rot[0] -= 5.0;
                            break;
                        }
                    }
                }
                default:
                {
                    kdDefaultEvent(event);
                    break;
                }
            }
        }

        EGLint width = 0; 
        EGLint height = 0;
        eglQuerySurface(example->egl.display, example->egl.surface, EGL_WIDTH, &width);
        eglQuerySurface(example->egl.display, example->egl.surface, EGL_HEIGHT, &height);
        gears_reshape(width, height);

        KDust t2 = kdGetTimeUST();
        deltatime = (KDfloat32)((t2 - t1) * 1e-9);
        t1 = t2;

        /* advance rotation for next frame */
        angle += 70.0f * deltatime; /* 70 degrees per second */
        if(angle > 3600.0f)
        {
            angle -= 3600.0f;
        }
        gears_draw(angle);
        exampleRun(example);

        /* Benchmark */
        totaltime += deltatime;
        frames++;
        if(totaltime > 5.0f)
        {
            kdLogMessagefKHR("%d frames in %3.1f seconds = %6.3f FPS\n", frames, totaltime, frames / totaltime);
            totaltime -= 5.0f;
            frames = 0;
        }
    }

    return exampleDestroy(example);
}
