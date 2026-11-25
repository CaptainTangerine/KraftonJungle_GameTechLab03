#pragma once

#include "Editor/UI/EditorWidget.h"

class FEditorControlWidget : public FEditorWidget
{
public:
	virtual void Render(float DeltaTime) override;
	virtual const char* GetWindowName() const override { return "Control Panel"; }
};
