
typedef enum{
	     MODEL_TYPE_POLYGONAL = 100,
	     MODEL_TYPE_TEXT = 200,
	     MODEL_TYPE_FONT = 300, // sets the font for every sub element
}model_type;

typedef enum{
	     MODEL_MODE_NONE = 0,
	     MODEL_MODE_COLOR = 1,
	     MODEL_MODE_OFFSET = 2,
	     MODEL_MODE_SCALE= 4
}model_mode;

typedef struct{
  vec2 dim_px;
  texture tex;
  bool loaded;
  bool alpha;
  char * path;
}text_cache;

distance_field * distance_field_load(scheme * sc, pointer obj);

typedef struct{
  model_type type;
  model_mode mode;
  blit3d_polygon * verts;
  blit3d_polygon * uvs;
  distance_field * distance_field;
  text_cache cache;
  u32 view_id;
  union{
    
  //distance_field_model * dist;
    struct {
      char * text;
    };
  };
  vec4 color;
  vec3 offset;
  vec3 rotation;
  vec3 scale;
  pointer tag;
}model;

typedef struct{
  // a view is essentially a model that is being drawn to a texture.
  // it can be static or non-static
  // an example of a non-static view could be a mirror
  // an example of a static view is a generated model, screenshot or picture.

  vec2 dim_px;
  texture tex;

  mat4 view_matrix;
  mat4 camera_matrix;
  u32 model;
  bool loaded;
  bool alpha;

}view;
struct _context{
  gl_window * win;
  blit3d_context * blit3d;
  int running;
  scheme * sc;

  audio_context * audio;
  model * models;
  u32 model_count;

  view * views;
  u32 view_count;

  u32_to_u32_table * model_to_sub_model;
  
  u32_to_u32_table * shown_models;
  
  u32 current_symbol;
  u32 current_sub_model;
  mat4 view_matrix;
  mat4 camera_matrix;

  vec4 bg_color;

  float dt;
  pointer events;
  u32 object_count;
} ;
extern context * current_context;


void init_audio_subsystem(context * ctx);
