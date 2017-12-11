#version 450 core
#define CAM_NEAR 0.1f
#define GRID_DIM_Z 256
#define AMBIENT_GLOBAL 0.2f

layout(set = 0, binding = 0) uniform readonly Material_properties {
    vec3 ambient;
    float padding;

    vec3 diffuse;
    float alpha;

    vec3 specular;
    float specular_exponent;

    vec3 emissive;
} mtl_in;

layout(set = 1, binding = 0) uniform sampler2D mtl_diffuse_map;
layout(set = 1, binding = 1) uniform sampler2D mtl_opacity_map;
layout(set = 1, binding = 2) uniform sampler2D mtl_specular_map;
layout(set = 1, binding = 3) uniform sampler2D mtl_normal_map;

layout(set = 2, binding = 0) uniform readonly UBO {
    mat4 view;
    mat4 normal;
    mat4 model;
    mat4 projection_clip;

    vec2 tile_size; // xy
    uvec2 grid_dim; // xy

    vec3 cam_pos;
    float cam_far;

    vec2 resolution;
    uint num_lights;
} ubo_in;

layout(set = 2, binding = 1, rgba32f) uniform readonly imageBuffer light_pos_ranges;
layout(set = 2, binding = 2, rgba8) uniform readonly imageBuffer light_colors;

layout(set = 3, binding = 0, r8ui) uniform readonly uimageBuffer grid_flags;
layout(set = 3, binding = 2, r32ui) uniform readonly uimageBuffer grid_light_counts;
layout(set = 3, binding = 4, r32ui) uniform readonly uimageBuffer grid_light_count_offsets;
layout(set = 3, binding = 5, r32ui) uniform readonly uimageBuffer light_list;

layout (location= 0) in vec4 world_pos_in;
layout (location= 1) in vec3 world_normal_in;
layout (location= 2) in vec3 world_tangent_in;
layout (location= 3) in vec3 world_bitangent_in;
layout (location= 4) in vec2 uv_in;

layout (location = 0) out vec4 frag_color;

uvec3 view_pos_to_grid_coord(vec2 frag_pos, float view_z) {
    vec3 c;
    c.xy = (frag_pos - 0.5f)/ ubo_in.tile_size;
    c.z = min(float(GRID_DIM_Z - 1), max(0.f, float(GRID_DIM_Z) * log((-view_z - CAM_NEAR) / (ubo_in.cam_far - CAM_NEAR) + 1.f)));
    return uvec3(c);
}

int grid_coord_to_grid_idx(uvec3 c) {
    return int(ubo_in.grid_dim.x * ubo_in.grid_dim.y * c.z + ubo_in.grid_dim.x * c.y + c.x);
}

void main()
{
    vec3 mtl_c_diffuse = texture(mtl_diffuse_map, uv_in).rgb * mtl_in.diffuse;
    vec3 ambient = AMBIENT_GLOBAL * mtl_in.ambient;

    mat3 invBTN = inverse(transpose(mat3(normalize(world_tangent_in), normalize(world_bitangent_in), normalize(world_normal_in))));
    vec3 normal_sample = texture(mtl_normal_map, uv_in).rgb * vec3(2.f) - vec3(1.f);
    vec3 world_normal = invBTN * normal_sample;

    vec3 v = normalize(ubo_in.cam_pos - world_pos_in.xyz);
    // only draw the face facing camera if it's transparent
    if (mtl_in.alpha < 1.f && dot(v, world_normal) < 0.f) {
	frag_color = vec4(0.f);
	return;
    }

    float r0 = 0.02f;
    float fresnel = max(0.f, r0 + (1.f - r0) * pow(1.f - max(dot(v, world_normal), 0.f), 5.f));
    vec3 mtl_c_specular = texture(mtl_specular_map, uv_in).rgb * mtl_in.specular;
    vec3 fresnel_specular = mtl_c_specular * (1.f - mtl_c_specular) * fresnel;

    vec3 view_pos = (ubo_in.view * vec4(world_pos_in.xyz, 1.f)).xyz;
    uvec3 grid_coord = view_pos_to_grid_coord(gl_FragCoord.xy, view_pos.z);
    int grid_idx = grid_coord_to_grid_idx(grid_coord);

    vec3 lighting = vec3(0.f);
    if (imageLoad(grid_flags, grid_idx).r == 1) {
	uint offset = imageLoad(grid_light_count_offsets, grid_idx).r;
	uint light_count = imageLoad(grid_light_counts, grid_idx).r;
	for (uint i = 0; i < light_count; i ++) {

	    int light_idx = int(imageLoad(light_list, int(offset + i)).r);
	    vec4 light_pos_range = imageLoad(light_pos_ranges, light_idx);
	    float dist = distance(light_pos_range.xyz, world_pos_in.xyz);
	    if (dist < light_pos_range.w) {
		vec3 l = normalize(light_pos_range.xyz - world_pos_in.xyz);
		vec3 h = normalize(0.5f * (v + l));

		float lambertien = max(dot(world_normal, l), 0.f);
		float atten = max(1.f - max(0.f, dist / light_pos_range.w), 0.f);

		vec3 specular;
		if (mtl_in.specular_exponent > 0.f) {
		    vec3 blinn_phong_specular = mtl_c_specular * pow(max(0.f, dot(h, world_normal)), mtl_in.specular_exponent);
		    specular = fresnel_specular + blinn_phong_specular;
		} else {
		    specular = 0.5f * fresnel_specular;
		}

		vec3 light_color = imageLoad(light_colors, light_idx).rgb;
		lighting += light_color * lambertien * atten * (mtl_c_diffuse + specular);
	    }
	}
    }
    frag_color = vec4(lighting + ambient, mtl_in.alpha + (1.f - mtl_in.alpha) * fresnel);
}
