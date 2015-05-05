/*******************************************************
 * Copyright (c) 2015-2019, ArrayFire
 * All rights reserved.
 *
 * This file is distributed under 3-clause BSD license.
 * The complete license agreement can be obtained at:
 * http://arrayfire.com/licenses/BSD-3-Clause
 ********************************************************/

#include <fg/histogram.h>
#include <fg/exception.h>
#include <common.hpp>
#include <err_common.hpp>
#include <cstdio>

#include <math.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace std;

const char *gHistBarVertexShaderSrc =
"#version 330\n"
"in vec2 point;\n"
"in float freq;\n"
"uniform float ymax;\n"
"uniform float nbins;\n"
"uniform mat4 transform;\n"
"void main(void) {\n"
"   float binId = gl_InstanceID;\n"
"   float deltax = 2.0f/nbins;\n"
"   float deltay = 2.0f/ymax;\n"
"   float xcurr = -1.0f + binId * deltax;\n"
"   if (point.x==1) {\n"
"        xcurr  += deltax;\n"
"   }\n"
"   float ycurr = -1.0f;\n"
"   if (point.y==1) {\n"
"       ycurr += deltay * freq;\n"
"   }\n"
"   gl_Position = transform * vec4(xcurr, ycurr, 0, 1);\n"
"}";

const char *gHistBarFragmentShaderSrc =
"#version 330\n"
"uniform vec4 barColor;\n"
"out vec4 outColor;\n"
"void main(void) {\n"
"   outColor = barColor;\n"
"}";

namespace fg
{

Histogram::Histogram(GLuint pNBins, GLenum pDataType)
    : Chart(), mDataType(pDataType), mNBins(pNBins),
    mHistogramVAO(0), mHistogramVBO(0), mHistogramVBOSize(0), mHistBarProgram(0),
    mHistBarMatIndex(0), mHistBarColorIndex(0), mHistBarYMaxIndex(0)
{
    CheckGL("Begin Histogram::Histogram");
    MakeContextCurrent();

    CheckGL("Begin Histogram::Shaders");
    mHistBarProgram = initShaders(gHistBarVertexShaderSrc, gHistBarFragmentShaderSrc);
    CheckGL("End Histogram::Shaders");

    GLuint pointIndex  = glGetAttribLocation (mHistBarProgram, "point");
    GLuint freqIndex   = glGetAttribLocation (mHistBarProgram, "freq");
    mHistBarColorIndex = glGetUniformLocation(mHistBarProgram, "barColor");
    mHistBarMatIndex   = glGetUniformLocation(mHistBarProgram, "transform");
    mHistBarNBinsIndex = glGetUniformLocation(mHistBarProgram, "nbins");
    mHistBarYMaxIndex  = glGetUniformLocation(mHistBarProgram, "ymax");

    switch(mDataType) {
        case GL_FLOAT:
            mHistogramVBO = createBuffer<float>(mNBins, NULL, GL_DYNAMIC_DRAW);
            mHistogramVBOSize = mNBins*sizeof(float);
            break;
        case GL_INT:
            mHistogramVBO = createBuffer<int>(mNBins, NULL, GL_DYNAMIC_DRAW);
            mHistogramVBOSize = mNBins*sizeof(int);
            break;
        case GL_UNSIGNED_INT:
            mHistogramVBO = createBuffer<unsigned>(mNBins, NULL, GL_DYNAMIC_DRAW);
            mHistogramVBOSize = mNBins*sizeof(unsigned);
            break;
        case GL_UNSIGNED_BYTE:
            mHistogramVBO = createBuffer<unsigned char>(mNBins, NULL, GL_DYNAMIC_DRAW);
            mHistogramVBOSize = mNBins*sizeof(unsigned char);
            break;
        default: fg::TypeError("Plot::Plot", __LINE__, 1, mDataType);
    }

    //create vao
    glGenVertexArrays(1, &mHistogramVAO);
    glBindVertexArray(mHistogramVAO);
    // attach histogram bar vertices
    glBindBuffer(GL_ARRAY_BUFFER, rectangleVBO());
    glVertexAttribPointer(pointIndex, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(pointIndex);
    // attach histogram frequencies
    glBindBuffer(GL_ARRAY_BUFFER, mHistogramVBO);
    glVertexAttribPointer(freqIndex, 1, mDataType, GL_FALSE, 0, 0);
    glVertexAttribDivisor(freqIndex, 1);
    glEnableVertexAttribArray(freqIndex);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CheckGL("End Histogram::Histogram");
}

Histogram::~Histogram()
{
    MakeContextCurrent();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &mHistogramVBO);
    glDeleteVertexArrays(1, &mHistogramVAO);
    glDeleteProgram(mHistBarProgram);
}

void Histogram::setBarColor(float r, float g, float b)
{
    mBarColor[0] = r;
    mBarColor[1] = g;
    mBarColor[2] = b;
    mBarColor[3] = 1.0f;
}

GLuint Histogram::vbo() const { return mHistogramVBO; }

size_t Histogram::size() const { return mHistogramVBOSize; }

void Histogram::render(int pX, int pY, int pVPW, int pVPH) const
{
    static const float BLACK[4] = {0,0,0,1};
    float w = pVPW - (leftMargin()+rightMargin()+tickSize());
    float h = pVPH - (bottomMargin()+topMargin()+tickSize());
    float offset_x = (2.0f * (leftMargin()+tickSize()) + (w - pVPW)) / pVPW;
    float offset_y = (2.0f * (bottomMargin()+tickSize()) + (h - pVPH)) / pVPH;
    float scale_x = w / pVPW;
    float scale_y = h / pVPH;

    CheckGL("Begin Histogram::render");
    /* Enavle scissor test to discard anything drawn beyond viewport.
     * Set scissor rectangle to clip fragments outside of viewport */
    glScissor(pX+leftMargin()+tickSize(), pY+bottomMargin()+tickSize(),
            pVPW - (leftMargin()+rightMargin()+tickSize()),
            pVPH - (bottomMargin()+topMargin()+tickSize()));
    glEnable(GL_SCISSOR_TEST);

    glm::mat4 trans = glm::translate(glm::scale(glm::mat4(1),
                                                glm::vec3(scale_x, scale_y, 1)),
                                     glm::vec3(offset_x, offset_y, 0));

    glUseProgram(mHistBarProgram);
    glUniformMatrix4fv(mHistBarMatIndex, 1, GL_FALSE, glm::value_ptr(trans));
    glUniform4fv(mHistBarColorIndex, 1, mBarColor);
    glUniform1f(mHistBarNBinsIndex, mNBins);
    glUniform1f(mHistBarYMaxIndex, ymax());

    /* render a rectangle for each bin. Same
     * rectangle is scaled and translated accordingly
     * for each bin. This is done by OpenGL feature of
     * instanced rendering */
    glBindVertexArray(mHistogramVAO);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, mNBins);
    glBindVertexArray(0);

    glUniform4fv(mHistBarColorIndex, 1, BLACK);
    glBindVertexArray(mHistogramVAO);
    glDrawArraysInstanced(GL_LINE_LOOP, 0, 4, mNBins);
    glBindVertexArray(0);

    glUseProgram(0);
    /* Stop clipping */
    glDisable(GL_SCISSOR_TEST);

    renderChart(pVPW, pVPH);
    CheckGL("End Histogram::render");
}

}
