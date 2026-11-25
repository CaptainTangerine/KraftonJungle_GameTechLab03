#pragma once

#include "Core/CoreTypes.h"

class UEditorEngine;

class FEditorWidget
{
public:
	virtual ~FEditorWidget() = default;

	// 전제 조건: InEditorEngine는 FEditorMainPanel이 위젯 수명을 관리하는 동안 계속 유효해야 한다.
	// 각 위젯은 에디터 질의를 위해 이 포인터를 캐시만 하며, 엔진 인스턴스를 소유하지는 않는다.
	virtual void Initialize(UEditorEngine* InEditorEngine);
	// 위젯이 소유한 일시적 UI 자원은 ImGui 백엔드/컨텍스트가 내려가기 전에 여기서 해제한다.
	virtual void Shutdown();
	virtual void Render(float DeltaTime) = 0;
	virtual const char* GetWindowName() const = 0;

	bool IsOpen() const { return bOpen; }
	void SetOpen(bool bInOpen) { bOpen = bInOpen; }
	bool* GetOpenStatePtr() { return &bOpen; }

protected:
	UEditorEngine* EditorEngine = nullptr;
	bool bOpen = true;
};
