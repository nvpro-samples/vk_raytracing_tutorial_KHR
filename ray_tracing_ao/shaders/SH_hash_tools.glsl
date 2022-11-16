#define HASH_MAP_SIZE 100000
#define S_MIN 0.0000000001

struct HashCell {
    float ao_value;             // the averaged ambient occlusion value in the given hash cell
    uint contribution_counter;  // number of samples contributing to value for blending in new values (old * cc/(cc+1) + new * 1 / (cc+1))
    uint checksum;              // checksum for deciding if cell should be resetted or contribution added
};

struct ConfigurationValues {
    vec3 camera_position;   // camera position
    uint s_nd;              // normal coarseness
    uint s_p;               // user-defined level of coarseness in pixel
    float f;                 // camera aperture
    uvec2 res;              // screen resolution in pixel
};

//
//  Hash Function: Fowler-Noll-Vo (FNV-1 hash 32-bit version)
//
const uint FNV_offset_basis = 0x811c9dc5;
const uint FNV_prime = 0x01000193;

uint[4] _dismemberFloat(float x){
    uint res[4];
    uint ri = floatBitsToUint(x);
    res[0] = ri & 0xFF000000 >> 24;
    res[1] = ri & 0x00FF0000 >> 16;
    res[2] = ri & 0x0000FF00 >> 8;
    res[3] = ri & 0x000000FF;
    return res;
}

uint _encode(uint hash, uint byte){
    hash = hash * FNV_prime;
    hash = hash ^ byte;
    return hash;
}

uint h1(float x){
    uint bytes[4] = _dismemberFloat(x);
    uint hash = 1;

    for(int i=0; i < 4; i++)
        hash = _encode(hash, bytes[i]);
    
    return hash/HASH_MAP_SIZE;
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
    return 0;
}

//
//  Multiple Dimension Hash Functions
//

//function to specifically adress different levels for blurr later ?
uint H4D_SWD(vec3 position, uint s_wd){
    return h1(s_wd 
        + h1(uint(position.z / s_wd) 
        + h1(uint(position.y / s_wd) 
        + h1(uint(position.x/ s_wd)))));
}

//hash function to hash points without normal
uint H4D(ConfigurationValues c, vec3 position){
    float dis = length(position - c.camera_position);
    float s_w = dis * tan(max(c.f / c.res.x, c.f * c.res.x / pow(c.res.y, 2)) * c.s_p);
    uint s_wd = uint(pow(2, uint(log2(s_w / S_MIN)) * S_MIN));
    return H4D_SWD(position, s_wd);
}

// Actually used all-inclusive function
uint H7D(ConfigurationValues c, vec3 position, vec3 normal){
    normal = normal * c.s_nd;
    ivec3 normal_d = ivec3(normal);
    return h1(normal_d.z 
        + h1(normal_d.y 
        + h1(normal_d.x 
        + H4D(c, position)))) % HASH_MAP_SIZE;

}

//
// Checksum functions
//

//function to specifically adress different levels for blurr later ?
uint H4D_SWD_checksum(vec3 position, uint s_wd){
    return h2(s_wd 
        + h2(uint(position.z / s_wd) 
        + h2(uint(position.y / s_wd) 
        + h2(uint(position.x/ s_wd)))));
}

//hash function to hash points with normal
uint H4D_checksum(ConfigurationValues c, vec3 position){
    float dis = length(position - c.camera_position);
    float s_w = dis * tan(max(c.f / c.res.x, c.f * c.res.x / pow(c.res.y, 2)) * c.s_p);
    uint s_wd = uint(pow(2, uint(log2(s_w / S_MIN)) * S_MIN));
    return H4D_SWD_checksum(position, s_wd);
}

// Actually used all-inclusive function for checksum
uint H7D_checksum(ConfigurationValues c, vec3 position, vec3 normal){
    normal = normal * c.s_nd;
    ivec3 normal_d = ivec3(normal);
    return h2(normal_d.z 
        + h2(normal_d.y 
        + h2(normal_d.x 
        + H4D_checksum(c, position))));
}







