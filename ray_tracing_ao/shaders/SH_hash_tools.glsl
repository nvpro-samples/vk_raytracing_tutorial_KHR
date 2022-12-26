#define HASH_MAP_SIZE 10000000
#define S_MIN 0.0000000001
#define LINEAR_SEARCH_LENGTH 10

struct HashCell {
    float ao_value;             // Accumulated AO value
    uint contribution_counter;  // Number of contributions to this cell
    uint checksum;              // Checksum for determining hash-collisions
    uint rc;                    // Replacement counter
};

struct ConfigurationValues {
    vec3 camera_position;   // camera position
    float s_nd;              // normal coarseness
    float s_p;               // user-defined level of coarseness in pixel
    float f;                 // camera aperture
    uvec2 res;              // screen resolution in pixel
};

uint pow2[] = {1, 2, 4, 8, 16, 32, 64, 128};

//
// Hash function: Wang Hash
//

uint wang_hash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

uint wang_hash(float key)
{
  return wang_hash(floatBitsToUint(key));
}



//
// Hash function: Murmur Hash
//

uint murmur_hash(float f) {
    uint x = floatBitsToUint(f);
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

//
// For debugging: Assign a color to each hash and s_wd
//

vec3 _dismemberUint3(uint x){
    uint res[3];
    res[0] = (x & 0xFFC00000) >> 22;
    res[1] = (x & 0x003FF800) >> 11;
    res[2] = (x & 0x000007FF);
    return vec3(res[0], res[1], res[2]);
}

vec4 hash_to_color(uint hash){
    vec3 dis = _dismemberUint3(hash);

    return vec4(dis.x / 1023, dis.y / 2047, dis.z / 2047, 1);
}

vec4 swd_to_color(float s_wd){
    
    if(s_wd >= 0 && s_wd <= 1){
        return vec4(0, s_wd * 10, 0, 1); 
    }
    if(s_wd < 0) {
        return vec4(s_wd * (-10), 0, 0, 1);
    }
    if(s_wd > 1){
        return vec4(0,0, (s_wd - 1), 1);
    }

}

//
// Calculate cell size in world-space
//

float s_wd_calc(ConfigurationValues c, vec3 position){
    float dis = distance(position, c.camera_position);
    float s_w = dis * tan(c.s_p * c.f * max(1 / c.res.y, c.res.y / pow(c.res.x, 2)));
    float log_step = floor(log2(s_w / S_MIN));
    return pow(2, log_step) * S_MIN;
}

//
//  Multiple Dimension Hash Functions
//

// Hash position at cell size
uint H4D_SWD(vec3 position, float s_wd){
    // Clamp to smallest cell size
    s_wd = max(s_wd, S_MIN);

    return wang_hash(floatBitsToUint(s_wd)
         + wang_hash(floatBitsToUint(floor(position.z / s_wd))
         + wang_hash(floatBitsToUint(floor(position.y / s_wd))
         + wang_hash(floatBitsToUint(floor(position.x / s_wd))))));
}

// Actual all-inclusive hash function for normal + position
uint H7D_SWD(ConfigurationValues c, vec3 position, vec3 normal, float s_wd){
    normal = normalize(normal) * c.s_nd;
    ivec3 normal_d = ivec3(normal);
    return wang_hash(normal_d.z
         + wang_hash(normal_d.y
         + wang_hash(normal_d.x
         + H4D_SWD(position, s_wd))));
}

//
// Checksum functions
//

// Checksum for position
uint H4D_SWD_checksum(vec3 position, float s_wd){
    // Clamp to smallest cell size
    s_wd = max(s_wd, S_MIN);

    return murmur_hash(s_wd)
        ^ murmur_hash(floor(position.z / s_wd))
        ^ murmur_hash(floor(position.y / s_wd))
        ^ murmur_hash(floor(position.x/ s_wd));
}

// Actual all-inclusive checksum function for normal + position
uint H7D_SWD_checksum(ConfigurationValues c, vec3 position, vec3 normal, float s_wd){
    normal = normal * c.s_nd;
    ivec3 normal_d = ivec3(normal);
    return murmur_hash(normal_d.z)
         ^ murmur_hash(normal_d.y)
         ^ murmur_hash(normal_d.x)
         ^ H4D_SWD(position, s_wd);
}

//
// Utility function for filtering
//

float random_in_range(float seed, float begin, float end){
    const int m = 1073741824;  // pow(2, 30)
    const float m_inv =  (1.0 / m);
    float rnd = ((float(murmur_hash(seed) % m) * m_inv) * (end - begin)) + begin;
    return rnd;
}

float gauss1(float mid, float offset, float sigma){
    const float sigma_squared = sigma * sigma;  // Cache intermediate result
    const float two_sigma_squared = 2 * sigma_squared;  // Pre-compute constant
    return exp(-(offset - mid) * (offset - mid) / two_sigma_squared);  // Avoid using the pow function
}

float gauss3(vec3 mid_pos, vec3 offset_pos, float sigma){
    const float sigma_squared = sigma * sigma;  // Cache intermediate result
    const float two_sigma_squared = 2 * sigma_squared;  // Pre-compute constant
    return exp(-(dot(offset_pos - mid_pos, offset_pos - mid_pos) / two_sigma_squared));
}