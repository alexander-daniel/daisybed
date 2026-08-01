#include "daisy_pod.h"

using namespace daisy;

// Knob class to handle value catching and mapping
class Knob {
public:
    Knob() : caught(false) {}
    
    void Init(float initValue, float minVal, float maxVal) {
        value = initValue;
        min = minVal;
        max = maxVal;
        caught = false;
    }
    
    bool Update(float knobValue) {
        // Check if knob has caught up to stored value
        if (!caught) {
            float normalizedStored = (value - min) / (max - min);
            caught = hasKnobCaught(knobValue, normalizedStored);
        }
        
        // Only update value if caught
        if (caught) {
            value = min + knobValue * (max - min);
            return true;
        }
        return false;
    }
    
    float GetValue() const { return value; }
    void Reset() { caught = false; }
    
private:
    bool hasKnobCaught(float knobValue, float storedValue) {
        const float threshold = 0.02f;
        return fabs(knobValue - storedValue) < threshold;
    }
    
    float value;
    float min;
    float max;
    bool caught;
};