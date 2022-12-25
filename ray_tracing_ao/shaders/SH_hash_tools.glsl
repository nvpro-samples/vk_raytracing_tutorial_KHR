#define HASH_MAP_SIZE 10000000
#define S_MIN 0.0000000001
#define LINEAR_SEARCH_LENGTH 10

struct HashCell {
    float ao_value;             // the averaged ambient occlusion value in the given hash cell
    uint contribution_counter;  // number of samples contributing to value for blending in new values (old * cc/(cc+1) + new * 1 / (cc+1))
    uint checksum;              // checksum for deciding if cell should be resetted or contribution added
    uint replacement_counter;   //counts up if a collision occurs, high counter indicates soon replacement
    float s_wd;      //debugging
    float s_wd_real;
    int j;
    int written;
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
// Hash function: Wang hash
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
// Hash function: murmur something
//

uint h0(float f) {
    uint x = floatBitsToUint(f);
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

//
//  Hash Function: Fowler-Noll-Vo (FNV-1 hash 32-bit version)
//
const uint FNV_offset_basis = 0x811c9dc5;
const uint FNV_prime = 0x01000193;

uint[4] _dismemberFloat(float x){
    uint res[4];
    uint ri = floatBitsToUint(x);
    res[0] = (ri & 0xFF000000) >> 24;
    res[1] = (ri & 0x00FF0000) >> 16;
    res[2] = (ri & 0x0000FF00) >> 8;
    res[3] = ri & 0x000000FF;
    return res;
}

uint[4] _dismemberUint(uint x){
    uint res[4];
    res[0] = (x & 0xFF000000) >> 24;
    res[1] = (x & 0x00FF0000) >> 16;
    res[2] = (x & 0x0000FF00) >> 8;
    res[3] = x & 0x000000FF;
    return res;
}

vec3 _dismemberUint3(uint x){
    uint res[3];
    res[0] = (x & 0xFFC00000) >> 22;
    res[1] = (x & 0x003FF800) >> 11;
    res[2] = (x & 0x000007FF);
    return vec3(res[0], res[1], res[2]);
}

uint _encode(uint hash, uint byte){
    hash = hash * FNV_prime;
    hash = hash ^ byte;
    return hash;
}

uint h1(uint x){
    uint bytes[4] = _dismemberUint(x);
    uint hash = 1;

    for(int i=0; i < 4; i++)
        hash = _encode(hash, bytes[i]);
    
    return hash;
}

//
//  Jenkings Hash Function
//

uint h2(float x){
    uint bytes[4] = _dismemberFloat(x);
    uint hash = 0;

    for(int i=0; i < 4; i++){
        hash += bytes[i];
        hash += hash << 10;
        hash  = hash ^ (hash >> 6);
    }
    hash += hash << 3;
    hash = hash ^ (hash >> 11);
    hash += hash << 15;
    return hash;
}

float s_w_calc(ConfigurationValues c, vec3 position){
    float dis = distance(position, c.camera_position);
    float s_w = dis * tan(c.s_p * c.f * max(1 / c.res.y, c.res.y / pow(c.res.x, 2)));
    return s_w;
}

float s_wd_calc(ConfigurationValues c, vec3 position){
    float dis = distance(position, c.camera_position);
    float s_w = dis * tan(c.s_p * c.f * max(1 / c.res.y, c.res.y / pow(c.res.x, 2)));
    float log_step = floor(log2(s_w / S_MIN));
    return pow(2, log_step) * S_MIN;
}


uint h(float f) {
    return h0(f);
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
//  Multiple Dimension Hash Functions
//
uint H4D_SWD(vec3 position, float s_wd){

    s_wd = max(s_wd, S_MIN);

    return wang_hash(floatBitsToUint(s_wd)
         + wang_hash(floatBitsToUint(floor(position.z / s_wd))
         + wang_hash(floatBitsToUint(floor(position.y / s_wd))
         + wang_hash(floatBitsToUint(floor(position.x / s_wd))))));
}

//hash function to hash points without normal
uint H4D(ConfigurationValues c, vec3 position){
    float s_wd = s_wd_calc(c, position);
    return H4D_SWD(position, s_wd);
}

// Actually used all-inclusive function
uint H7D(ConfigurationValues c, vec3 position, vec3 normal){
    normal = normal * c.s_nd;
    ivec3 normal_d = ivec3(normal);
    return wang_hash(normal_d.z
         + wang_hash(normal_d.y
         + wang_hash(normal_d.x
         + H4D(c, position))));

}

// Actually used all-inclusive function
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

//function to specifically adress different levels for blurr later ?
uint H4D_SWD_checksum(vec3 position, float s_wd){
    return h0(s_wd)
        ^ h0(floor(position.z / s_wd))
        ^ h0(floor(position.y / s_wd))
        ^ h0(floor(position.x/ s_wd));
}

// hash function to hash points with normal
uint H4D_checksum(ConfigurationValues c, vec3 position){
    float s_wd = s_wd_calc(c, position);
    return H4D_SWD_checksum(position, s_wd);
}

// Actually used all-inclusive function for checksum
uint H7D_checksum(ConfigurationValues c, vec3 position, vec3 normal){
    normal = normal * c.s_nd;
    ivec3 normal_d = ivec3(normal);
    return h0(normal_d.z)
        ^ h0(normal_d.y)
        ^ h0(normal_d.x)
        ^ H4D_checksum(c, position);
}

// Actually used all-inclusive function
uint H7D_SWD_checksum(ConfigurationValues c, vec3 position, vec3 normal, float s_wd){
    normal = normal * c.s_nd;
    ivec3 normal_d = ivec3(normal);
    return h0(normal_d.z)
         ^ h0(normal_d.y)
         ^ h0(normal_d.x)
         ^ H4D_SWD(position, s_wd);
}

float random_in_range(float seed, float begin, float end){
    int m = int(pow(2, 30));
    float rnd = ((float(h0(seed) % m) / m) * (end - begin)) + begin;
    return rnd;
}

float gauss1(float mid, float offset, float sigma){
    
    return exp(-(pow(offset - mid, 2) / (2 * pow(sigma, 2))));
}

float gauss3(vec3 mid_pos, vec3 offset_pos, float sigma){
    
    return exp(-(  (pow(offset_pos.x - mid_pos.x, 2) + pow(offset_pos.y - mid_pos.y, 2) + pow(offset_pos.z - mid_pos.z, 2)) / (2 * pow(sigma, 2))));
}