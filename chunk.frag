#version 330 core

in vec2 FragTexCoord;
in vec4 FragColor;
in float FragLayer;

out vec4 FinalColor;

uniform sampler2DArray textureArray;

void main() {
    vec4 texColor = texture(textureArray, vec3(FragTexCoord, FragLayer));

    // Discard transparent pixels so they don't occlude geometry behind plant leaves
    if (texColor.a < 0.1) {
        discard;
    }

    FinalColor = texColor * FragColor;
}