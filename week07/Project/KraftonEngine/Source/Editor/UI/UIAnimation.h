#pragma once

#include <algorithm>

class FUIAnimation
{
public:
	float GetValue() const { return Value; }
	void SetValue(float InValue) { Value = std::clamp(InValue, 0.0f, 1.0f); }
	void UpdateTowards(float InTarget, float DeltaTime, float Speed)
	{
		const float Target = std::clamp(InTarget, 0.0f, 1.0f);
		if (Value < Target)
		{
			Value = (std::min)(Target, Value + DeltaTime * Speed);
		}
		else if (Value > Target)
		{
			Value = (std::max)(Target, Value - DeltaTime * Speed);
		}
	}
	bool IsNearlyHidden(float Epsilon = 0.001f) const { return Value <= Epsilon; }

private:
	float Value = 0.0f;
};
