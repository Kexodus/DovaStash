#pragma once

// Inputs of the widget gallery (DrawDemo, Widgets.h) that only Window.cpp has: the device for thumbnails
// and a loaded scene (Breezehome) to take them from.

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace dova::editor::render
{
	class Scene;
}

namespace dova::editor::ui::w
{
	void SetDemoDevice(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	void SetDemoScene(std::shared_ptr<render::Scene> a_scene);  // any thread
	void ReleaseDemo();                                           // before the device goes away
}
