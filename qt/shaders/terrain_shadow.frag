#version 440
layout(location = 0) in float opacity;
void main()
{
    // Fully faded outskirts must not cast an invisible solid disk. This
    // deterministic cutoff avoids noisy/dithered shadows on the translucent rim.
    if (opacity < 0.55) discard;
}
