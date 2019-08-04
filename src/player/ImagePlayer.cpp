/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019
              Vladimír Vondruš <mosra@centrum.cz>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include <sstream>
#include <Corrade/Containers/Optional.h>
#include <Corrade/Utility/Directory.h>
#include <Corrade/Utility/FormatStl.h>
#include <Magnum/ImageView.h>
#include <Magnum/PixelFormat.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/Texture.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/MeshTools/Compile.h>
#include <Magnum/Primitives/Square.h>
#include <Magnum/Shaders/Flat.h>
#include <Magnum/Trade/AbstractImporter.h>
#include <Magnum/Trade/ImageData.h>
#include <Magnum/Trade/MeshData2D.h>

#include "Magnum/Text/Alignment.h"
#include "Magnum/Ui/Anchor.h"
#include "Magnum/Ui/Label.h"
#include "Magnum/Ui/Plane.h"
#include "Magnum/Ui/UserInterface.h"

#include "AbstractPlayer.h"

namespace Magnum { namespace Player {

namespace {

constexpr const Float LabelHeight{36.0f};
constexpr const Vector2 LabelSize{72.0f, LabelHeight};

struct BaseUiPlane: Ui::Plane {
    explicit BaseUiPlane(Ui::UserInterface& ui):
        Ui::Plane{ui, Ui::Snap::Top|Ui::Snap::Bottom|Ui::Snap::Left|Ui::Snap::Right, 1, 50, 640},
        imageInfo{*this, {Ui::Snap::Top|Ui::Snap::Left, LabelSize}, "", Text::Alignment::LineLeft, 128, Ui::Style::Dim} {}

    Ui::Label imageInfo;
};

class ImagePlayer: public AbstractPlayer {
    public:
        explicit ImagePlayer(Platform::ScreenedApplication& application, Ui::UserInterface& uiToStealFontFrom);

    private:
        void drawEvent() override;
        void viewportEvent(ViewportEvent& event) override;

        void keyPressEvent(KeyEvent& event) override;
        void mousePressEvent(MouseEvent& event) override;
        void mouseReleaseEvent(MouseEvent& event) override;
        void mouseMoveEvent(MouseMoveEvent& event) override;
        void mouseScrollEvent(MouseScrollEvent& event) override;

        void load(const std::string& filename, Trade::AbstractImporter& importer) override;
        void setControlsVisible(bool visible) override;

        void initializeUi();
        Vector2 unproject(const Vector2i& windowPosition) const;
        Vector2 unprojectRelative(const Vector2i& relativeWindowPosition) const;

        Shaders::Flat2D _coloredShader;

        /* UI */
        Containers::Optional<Ui::UserInterface> _ui;
        Containers::Optional<BaseUiPlane> _baseUiPlane;
        std::string _imageInfo;

        GL::Texture2D _texture{NoCreate};
        GL::Mesh _square;
        Shaders::Flat2D _shader{Shaders::Flat2D::Flag::Textured};
        Vector2i _imageSize;
        Matrix3 _transformation;
        Matrix3 _projection;
};

ImagePlayer::ImagePlayer(Platform::ScreenedApplication& application, Ui::UserInterface& uiToStealFontFrom): AbstractPlayer{application, PropagatedEvent::Draw|PropagatedEvent::Input} {
    /* Setup the UI, steal font etc. from the existing one to avoid having
       everything built twice */
    /** @todo this is extremely bad, there should be just one global UI (or
        not?) */
    _ui.emplace(Vector2(application.windowSize())/application.dpiScaling(), application.windowSize(), application.framebufferSize(), uiToStealFontFrom.font(), uiToStealFontFrom.glyphCache(), Ui::mcssDarkStyleConfiguration());
    initializeUi();

    /* Prepare the square mesh and initial projection equal to framebuffer size */
    _square = MeshTools::compile(Primitives::squareSolid(Primitives::SquareTextureCoords::Generate));
    _projection = Matrix3::projection(Vector2{application.framebufferSize()});
}

void ImagePlayer::drawEvent() {
    #ifdef MAGNUM_TARGET_WEBGL /* Another FB could be bound from the depth read */
    GL::defaultFramebuffer.bind();
    #endif
    GL::defaultFramebuffer.clear(GL::FramebufferClear::Color|GL::FramebufferClear::Depth);

    _shader.bindTexture(_texture)
        .setTransformationProjectionMatrix(_projection*_transformation);
    _square.draw(_shader);

    /* Draw the UI. Disable the depth buffer and enable premultiplied alpha
       blending. */
    {
        GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
        GL::Renderer::enable(GL::Renderer::Feature::Blending);
        GL::Renderer::setBlendFunction(GL::Renderer::BlendFunction::One, GL::Renderer::BlendFunction::OneMinusSourceAlpha);
        _ui->draw();
        GL::Renderer::setBlendFunction(GL::Renderer::BlendFunction::One, GL::Renderer::BlendFunction::Zero);
        GL::Renderer::disable(GL::Renderer::Feature::Blending);
        GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    }
}

void ImagePlayer::viewportEvent(ViewportEvent& event) {
    _baseUiPlane = Containers::NullOpt;
    _ui->relayout(Vector2(event.windowSize())/event.dpiScaling(), event.windowSize(), event.framebufferSize());
    initializeUi();

    setControlsVisible(controlsVisible());
    _baseUiPlane->imageInfo.setText(_imageInfo);
    _projection = Matrix3::projection(Vector2{event.framebufferSize()});
}

void ImagePlayer::keyPressEvent(KeyEvent& event) {
    if(event.key() == KeyEvent::Key::NumZero) {
        _transformation = Matrix3::scaling(Vector2{_imageSize}/2.0f);
    } else return;

    event.setAccepted();
    redraw();
}

void ImagePlayer::mousePressEvent(MouseEvent& event) {
    if(_ui->handlePressEvent(event.position())) {
        redraw();
        event.setAccepted();
        return;
    }
}

void ImagePlayer::mouseReleaseEvent(MouseEvent& event) {
    if(_ui->handleReleaseEvent(event.position())) {
        redraw();
        event.setAccepted();
        return;
    }
}

Vector2 ImagePlayer::unproject(const Vector2i& windowPosition) const {
    CORRADE_INTERNAL_ASSERT(application());

    /* Normalize from window-relative position with origin at top left and Y
       down to framebuffer-relative position with origin at center and Y going
       up */
    return (Vector2{windowPosition}/Vector2{application()->windowSize()} - Vector2{0.5f})*Vector2{application()->framebufferSize()}*Vector2::yScale(-1.0f);
}

Vector2 ImagePlayer::unprojectRelative(const Vector2i& relativeWindowPosition) const {
    CORRADE_INTERNAL_ASSERT(application());

    /* Only resizing for framebuffer-relative position and Y going up instead
       of down, no origin movements */
    return Vector2{relativeWindowPosition}*Vector2{application()->framebufferSize()}*Vector2::yScale(-1.0f)/Vector2{application()->windowSize()};
}

void ImagePlayer::mouseMoveEvent(MouseMoveEvent& event) {
    if(_ui->handleMoveEvent(event.position())) {
        redraw();
        event.setAccepted();
        return;
    }

    if(!(event.buttons() & MouseMoveEvent::Button::Left)) return;

    const Vector2 delta = unprojectRelative(event.relativePosition());
    _transformation = Matrix3::translation(delta)*_transformation;
    event.setAccepted();
    redraw();
}

void ImagePlayer::mouseScrollEvent(MouseScrollEvent& event) {
    if(!event.offset().y()) return;

    /* Zoom to selection point -- translate that point to origin, scale,
       translate back */
    const Vector2 projectedPosition = unproject(event.position());
    _transformation =
        Matrix3::translation(projectedPosition)*
        Matrix3::scaling(Vector2{1.0f + 0.1f*event.offset().y()})*
        Matrix3::translation(-projectedPosition)*
        _transformation;

    event.setAccepted();
    redraw();
}

void ImagePlayer::load(const std::string& filename, Trade::AbstractImporter& importer) {
    Containers::Optional<Trade::ImageData2D> image = importer.image2D(0);
    if(!image) return;

    _texture = GL::Texture2D{};
    _texture
        .setMagnificationFilter(GL::SamplerFilter::Nearest)
        .setMinificationFilter(GL::SamplerFilter::Linear)
        .setWrapping(GL::SamplerWrapping::ClampToEdge)
        .setStorage(1, GL::TextureFormat::RGBA8, image->size())
        .setSubImage(0, {}, *image);

    /* Set up default transformation (1:1 scale, centered) */
    _imageSize = image->size();
    if(_transformation == Matrix3{})
        _transformation = Matrix3::scaling(Vector2{_imageSize}/2.0f);

    /* Populate the model info */
    /** @todo ugh debug->format converter?! */
    std::ostringstream out;
    Debug{&out, Debug::Flag::NoNewlineAtTheEnd} << image->format();
    _baseUiPlane->imageInfo.setText(_imageInfo = Utility::formatString(
        "{}: {}x{}, {}",
        Utility::Directory::filename(filename).substr(0, 32),
        image->size().x(), image->size().y(),
        out.str()));
}

void ImagePlayer::setControlsVisible(bool visible) {
    _baseUiPlane->imageInfo.setVisible(visible);
}

void ImagePlayer::initializeUi() {
    _baseUiPlane.emplace(*_ui);
}

}

Containers::Pointer<AbstractPlayer> createImagePlayer(Platform::ScreenedApplication& application, Ui::UserInterface& uiToStealFontFrom) {
    return Containers::Pointer<ImagePlayer>{Containers::InPlaceInit, application, uiToStealFontFrom};
}

}}