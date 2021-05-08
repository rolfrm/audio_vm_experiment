#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <iron/full.h>
#include <iron/gl.h>
#include <iron/audio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <GL/gl.h>
#include "scheme.h"
#include "scheme-private.h"
#include "u32_to_u32_table.h"
#include "ptr_to_u32_table.h"
#include "main.h"

#include "internal.h"
#define ULONG_MAX 0xFFFFFFFF



context * current_context;
void push_key_event(context * ctx, int key);

mat4 mat3_rotate_3d_transform(float x, float y, float z, float rx, float ry, float rz){
  var m = mat4_identity();
  m.data[3][0] = x;
  m.data[3][1] = y;
  m.data[3][2] = z;
  m = mat4_rotate_X(m, rx);
  m = mat4_rotate_Y(m, ry);
  m = mat4_rotate_Z(m, rz);
  return m;
}

bool context_running(context * ctx){
  current_context = ctx;
  return ctx->running;
}

mat4 mat4_translate_vec3(vec3 v){
  return mat4_translate(v.x, v.y, v.z);
}

void print_object_sub_models(){
  var ctx = current_context;
  for(u32 i = 0; i < ctx->model_to_sub_model->count; i++){
    int j = i + 1;
    var k = ctx->model_to_sub_model->key[j];
    var v = ctx->model_to_sub_model->value[j];
    printf("%i -> %i\n", k, v);
  }
}
vec3 v3_one = {.x = 1, .y = 1, .z = 1};
mat4 get_object_transform(model * object){
  
  var o = object->offset.data;
  var r = object->rotation.data;
  var s = object->scale;
  var O = mat3_rotate_3d_transform(o[0], o[1], o[2], r[0], r[1], r[2]);
  if(vec3_compare(v3_one, s, 0.0001) == false){
    O = mat4_mul(O, mat4_scaled(s.x, s.y, s.z));
  }
  return O;
}

int render_it = 0;

bool alpha_pass = false;
void render_model(context * ctx, mat4 transform, u32 id, vec4 color){
  //print_object_sub_models();
  //ERROR("!!");
  if(render_it > 5) ERROR("Stack\n");
  render_it += 1;
  var blit3d = ctx->blit3d;
  var object =  ctx->models + id;

  var O = get_object_transform(object);
  /*
  var o = object->offset.data;
  var r = object->rotation.data;
  var s = object->scale;
  var O = mat3_rotate_3d_transform(o[0], o[1], o[2], r[0], r[1], r[2]);
  if(vec3_compare(v3_one, s, 0.0001) == false){
    O = mat4_mul(O, mat4_scaled(s.x, s.y, s.z));

    }*/
  if(object->mode & MODEL_MODE_COLOR)
    color = vec4_mul(object->color, color);
  if(color.w <= 0.0)
    return;
  if(color.w < 1.0 && alpha_pass == false) return;
  
  mat4 C = mat4_invert(ctx->camera_matrix);
  
  O = mat4_mul(transform, O);
  var view = ctx->views + object->view_id;
  if(object->view_id != 0){
    if(view->loaded == false && view->model != 0){
      var dim = view->dim_px;
      if(dim.x <= 0.5 || dim.y <= 0.5)
	dim = view->dim_px = vec2_new(512, 512);
      blit_framebuffer buf = {.width = (int)dim.x, .height = (int)dim.y,
			      .channels = view->alpha ? 4 : 3};
      
      blit_begin(BLIT_MODE_PIXEL_SCREEN);
      blit_create_framebuffer(&buf);
      blit_use_framebuffer(&buf);


      view->loaded = true;
      
      glViewport(0, 0, dim.x, dim.y);
      glClearColor(0.0, 0.0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_BLEND);
      blit3d_context_load(blit3d);
      bool pass = alpha_pass;
      var view_matrix = ctx->view_matrix;
      var camera_matrix = ctx->camera_matrix;
      ctx->view_matrix = view->view_matrix;
      ctx->camera_matrix = view->camera_matrix;
      glDisable(GL_BLEND);      
      glEnable(GL_DEPTH_TEST);
      alpha_pass = false;
      render_model(ctx, mat4_identity(), view->model, vec4_new(1,1,1,1));
      alpha_pass = true;
      glEnable(GL_BLEND);

      render_model(ctx, mat4_identity(), view->model, vec4_new(1,1,1,1));
      blit_unuse_framebuffer(&buf);
      blit3d_context_load(blit3d);

      alpha_pass = pass;
      ctx->view_matrix = view_matrix;
      ctx->camera_matrix = camera_matrix;
      
      view->tex = blit_framebuffer_as_texture(&buf);
      
    }
  }
  if(object->view_id == 0 || view->loaded == false)
    view = NULL;
  
  if(object->cache.loaded == false){
    if(object->cache.path != NULL){
      image img = image_from_file(object->cache.path);
      if(img.width != 0){
	object->cache.tex = texture_from_image(&img);
	printf("loaded image: %i\n", img.channels);
	if(img.channels == 4)
	  object->cache.alpha = true;
	image_delete(&img);
	object->cache.loaded = true;
	
      }else{
	printf("Failed to load texture from \"%s\"\n", object->cache.path);
      }
    }
  if(object->type == MODEL_TYPE_TEXT && object->text != NULL && alpha_pass){
    printf("Loading texture cache\n");
    vec2 dim = blit_measure_text(object->text);
    vec2_print(dim);logd("\n");
    if(dim.x > 0 && dim.y > 0){
    
    object->cache.dim_px = dim;
    object->cache.loaded = true;
    object->cache.alpha = true;
    
    blit_framebuffer buf = {.width = (int)dim.x, .height = (int)dim.y,
			    .channels = 4};
    
    blit_begin(BLIT_MODE_PIXEL_SCREEN);
    blit_create_framebuffer(&buf);
    blit_use_framebuffer(&buf);
    
    glViewport(0, 0, dim.x, dim.y);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    blit_begin(BLIT_MODE_PIXEL_SCREEN);
    blit_color(1, 1, 1, 1);
    blit_text(object->text);
    blit_unuse_framebuffer(&buf);
    
    blit3d_context_load(blit3d);
    object->verts = blit3d_polygon_new();
    float aspect = dim.x / dim.y;
    
    float xy[] = {-1 * aspect,-1,0, 1 * aspect,-1,0, -1 * aspect,1,0, 1 * aspect,1,0};
    
    blit3d_polygon_load_data(object->verts, xy, 12 * sizeof(f32));
    blit3d_polygon_configure(object->verts, 3);
    glEnable(GL_BLEND);
    object->uvs = blit3d_polygon_new();
    float uv[] = {0,0, 1,0,0,1,1,1};
    blit3d_polygon_load_data(object->uvs, uv, 8 * sizeof(f32));
    blit3d_polygon_configure(object->uvs, 2);
    object->cache.tex = blit_framebuffer_as_texture(&buf);
    }
    }
  }
  
  if((object->type == MODEL_TYPE_POLYGONAL || object->type == MODEL_TYPE_TEXT) && object->verts != NULL){
    bool isalpha = color.w < 1.0 || (object->cache.loaded && object->cache.alpha) || (view != NULL && view->alpha);

    
    if((!isalpha && alpha_pass == false) || (isalpha && alpha_pass)){
      mat4 V = ctx->view_matrix;
      // World coord: Object * Vertex
      // Camera coord: InvCamera * World
      // View coord: View * Camera(ve
      // Vertx * Object * InvCamera * View
      vertex_buffer * buffers[2] = {object->verts, object->uvs};
      int bufcnt = 1;
      if(object->uvs != NULL)
	bufcnt = 2;
      if(object->cache.loaded)
	blit3d_bind_texture(blit3d, &object->cache.tex);
      else if(view != NULL){
	blit3d_bind_texture(blit3d, &view->tex);
      }
      blit3d_view(blit3d, mat4_mul(V, mat4_mul(C, O)));
      blit3d_color(blit3d, color);
      blit3d_polygon_blit2(blit3d, buffers, bufcnt);
      if(object->cache.loaded)
	blit3d_bind_texture(blit3d, NULL);
      else if(view != NULL){
	blit3d_bind_texture(blit3d, NULL);
      }
    }
  }

  int err = glGetError();
  if(err != 0)
    printf("ERR? %i\n", err);
  
  size_t indexes[10];
  size_t cnt;
  size_t index = 0;
  while((cnt = u32_to_u32_table_iter(ctx->model_to_sub_model, &id, 1, NULL, indexes, array_count(indexes), &index))){
    for(size_t i = 0 ; i < cnt; i++){
      var sub = ctx->model_to_sub_model->value[indexes[i]];
      //printf("render sub\n");

      if(sub == id){
	print_object_sub_models();
	ERROR("LOOP\n");
      }
      render_model(ctx, O, sub,  color);
      //printf("render done\n");
    }
  }

  render_it -= 1;
}

void render_update(context * ctx, float dt){
  { // handle events
    gl_window_event evt[10];
    size_t cnt = gl_get_events(evt, array_count(evt));
    for(size_t i = 0; i < cnt; i++){
      if(evt[i].type == EVT_KEY_DOWN){
	printf("%i %i\n", evt[i].key.scancode, evt[i].key.key, KEY_LEFT);
	push_key_event(ctx, evt[i].key.key);
      }
    }
  }
  //printf("Update!\n");
  current_context = ctx;
  //audio_update_streams(ctx->audio);
  scheme_load_string(ctx->sc, "(update)");
  

  current_context->dt = dt;

  gl_window_make_current(ctx->win);
  
  blit_begin(BLIT_MODE_UNIT);
  var blit3d = ctx->blit3d;
  var bg_color = ctx->bg_color;
  glClearColor(bg_color.x,bg_color.y,bg_color.z,bg_color.w);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glCullFace(GL_BACK);
  //glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  
  blit3d_context_load(blit3d);
  alpha_pass = false;
  for(u32 i = 0; i < ctx->shown_models->count; i++){
    render_it = 0;
    render_model(ctx, mat4_identity(), ctx->shown_models->key[i + 1], vec4_new(1,1,1,1));  
  }
  return;
  alpha_pass = true;
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  for(u32 i = 0; i < ctx->shown_models->count; i++){
    render_it = 0;
    render_model(ctx, mat4_identity(), ctx->shown_models->key[i + 1], vec4_new(1,1,1,1));  
  }

}


pointer set_bg_color(scheme * sc, pointer args){
  size_t count;
  f32 * colors = pointer_to_floats(sc, args, &count);
  if(count == 4)
    current_context->bg_color = vec4_new(colors[0] ,colors[1], colors[2], colors[3]);
  free(colors);
  return sc->NIL;
}


pointer print_result(scheme * sc, pointer data){
  printf("\nhej?? %i %i \n", pair_cdr(data) == sc->NIL, 0);
  return data;
}

u32 model_new(){
  u32 newid = current_context->model_count;
  current_context->model_count += 1;
  current_context->models = realloc(current_context->models, sizeof(current_context->models[0]) * current_context->model_count);
  current_context->models[newid] = (model){0};
  current_context->models[newid].tag = current_context->sc->NIL;
  current_context->models[newid].scale = vec3_new(1,1,1);
  current_context->models[newid].color = vec4_new(1,1,1,1);
  current_context->models[newid].type = 0;
  return newid;
}

u32 view_new(){
  u32 newid = current_context->view_count;
  current_context->view_count += 1;
  current_context->views = realloc(current_context->views, sizeof(current_context->views[0]) * current_context->view_count);
  current_context->views[newid] = (view){0};
  var view = &current_context->views[newid];
  view->view_matrix = mat4_identity();
  view->camera_matrix = mat4_identity();

  return newid;
}


bool get_view_id_from_pair(pointer pair, u32 * out){
  var car = pair_car(pair);
  if(is_symbol(car) && strcmp(symname(car), "view") == 0){
    var modelid = pair_cdr(pair);
    ASSERT(is_integer(modelid));
    *out = (u32)ivalue(modelid);
    return true;
  }
  return false;
}

bool get_model_id_from_pair(pointer pair, u32 * out){
  var car = pair_car(pair);
  if(is_symbol(car) && strcmp(symname(car), "model") == 0){
    var modelid = pair_cdr(pair);
    ASSERT(is_integer(modelid));
    *out = (u32)ivalue(modelid);
    return true;
  }
  return false;
}

bool get_model_id_from_args(pointer args, u32 * r){
  var ptr = pair_car(args);
  // (model model-id)
  return get_model_id_from_pair(ptr, r);
}

pointer show_model(scheme * sc, pointer args){

  u32 id;
  if(get_model_id_from_args(args, &id)){
    u32_to_u32_table_set(current_context->shown_models, id, 0);
    printf("show model %i\n", id);
  }
  return sc->NIL;
}

pointer unshow_model(scheme * sc, pointer args){
  u32 id;
  if(get_model_id_from_args(args, &id)){
    u32_to_u32_table_unset(current_context->shown_models, id);
    printf("show model %i\n", id);
  }
  return sc->NIL; 

}

pointer offset_model(scheme * sc, pointer args){
  size_t count;
  f32 * f = pointer_to_floats(sc, args, &count);
  vec3 offset;
  if(count == 2){
    offset = vec3_new(f[0], f[1], 0);
  }else if(count == 3){
    offset = vec3_new(f[0], f[1], f[2]);
  }else{
    goto end;
  }
  var object =  current_context->models + current_context->current_symbol;
  object->offset = offset;
 end:;
  free(f);
  return sc->NIL;       
}


pointer set_perspective(scheme * sc, pointer args){
  size_t count;
  f32 * fargs = pointer_to_floats(sc, args, &count);
  // y_fov, aspect, near, far
  if(count == 4){
    current_context->view_matrix = mat4_perspective(fargs[0], fargs[1], fargs[2], fargs[3]);
  }
  free(fargs);
  return sc->NIL; 
}


pointer set_orthographic(scheme * sc, pointer args){
  size_t count;
  f32 * fargs = pointer_to_floats(sc, args, &count);
  // y_fov, aspect, near, far
  if(count == 6)
    current_context->view_matrix = mat4_ortho(fargs[0], fargs[1], fargs[2], fargs[3], fargs[4], fargs[5]);
  if(count == 3)
    current_context->view_matrix = mat4_ortho(- 0.5 * fargs[0], 0.5 * fargs[0], -0.5 * fargs[1], 0.5 * fargs[1], -0.5 * fargs[2], 0.5 * fargs[2]);  
  free(fargs);
  return sc->NIL; 
}


pointer set_camera(scheme * sc, pointer args){
  size_t count;
  f32 * fargs = pointer_to_floats(sc, args, &count);
  // pos:x y z rot: x y z
  if(count == 6){
    
    var m = mat3_rotate_3d_transform(fargs[0], fargs[1], fargs[2],
				       fargs[3], fargs[4], fargs[5]);
    current_context->camera_matrix = m;
  }
  free(fargs);
  return sc->NIL;
}

void create_sub_model(u32 model, u32 sub_model){
  size_t indexes[10];
  size_t cnt;
  size_t index = 0;
  var ctx = current_context;
  while((cnt = u32_to_u32_table_iter(ctx->model_to_sub_model, &model, 1, NULL, indexes, array_count(indexes), &index))){
    for(size_t i = 0 ; i < cnt; i++){
      u32 ref = ctx->model_to_sub_model->value[indexes[i]];
      if(ref == sub_model)
	return;
    }
  }
  u32_to_u32_table_set(ctx->model_to_sub_model, model, sub_model);
}

pointer key_is_down(scheme * sc, pointer args){
  if(is_integer(pair_car(args)) == false)
    ERROR("Key is not integer");
  int keyid = ivalue(pair_car(args));
  bool isdown = gl_window_get_key_state(current_context->win, keyid);
  if(isdown)
    return sc->T;
  return sc->F;
}

pointer vec3_to_scheme(scheme * sc, vec3 v){
  pointer s_vec = sc->vptr->mk_vector(sc, 3);
  for(int i = 0; i < 3; i++){
    sc->vptr->set_vector_elem(s_vec, i, sc->vptr->mk_real(sc, v.data[i]));
  }
  return s_vec;
}

pointer camera_direction(scheme * sc, pointer args){
  var m = current_context->camera_matrix;
  for(int i = 0; i < 3; i++)
    m.data[3][i] = 0;
  vec3 v = mat4_mul_vec3(m, vec3_new(0,0,1));
  return vec3_to_scheme(sc, v);
}

pointer camera_right(scheme * sc, pointer args){
  var m = current_context->camera_matrix;
  for(int i = 0; i < 3; i++)
    m.data[3][i] = 0;
  m = mat4_rotate_Y(m, PI / 2.0);
  vec3 v = mat4_mul_vec3(m, vec3_new(0,0,1));
  return vec3_to_scheme(sc, v);
}

pointer get_dt(scheme * sc, pointer args){
  return sc->vptr->mk_real(sc, current_context->dt);
}

pointer config_model(scheme * sc, pointer args){
  pointer ptr = pair_car(args);
  u32 id;

  if(get_view_id_from_pair(ptr, &id)){
    var object =  current_context->views + id;
    ptr = pair_cdr(args);    
    while(ptr != sc->NIL){
      pointer pt2 = pair_car(ptr); //(poly 1 2 3)
      pointer sym = pair_car(pt2);
      char * symchars = symname(sym);
      if(is_symbol(sym)){
	bool ismodel = strcmp(symchars, "model") == 0;
	bool isortho = strcmp(symchars, "ortho") == 0;
	bool issize = strcmp(symchars, "size") == 0;
	bool isalpha = strcmp(symchars, "alpha") == 0;
	bool isdf = strcmp(symchars, "distance-field") == 0;
	ASSERT(!isdf);
	if(ismodel){
	  u32 subid;
	  
	  if(get_model_id_from_pair(pt2, &subid)){
	    object->model = subid;
	  }
	}
	if(isalpha)
	  object->alpha = true;
	if(isortho || issize){
	  size_t c = 0;
	  f32 * fs = pointer_to_floats(sc, pair_cdr(pt2), &c);
	  if(issize){
	    if(c == 1)
	      object->dim_px = vec2_new(fs[0], fs[0]); 
	    else if(c >= 2)
	      object->dim_px = vec2_new(fs[0], fs[1]); 
	  }
	  else if(isortho){
	    if(c == 6)
	      object->view_matrix = mat4_ortho(fs[0], fs[1], fs[2], fs[3], fs[4], fs[5]);
	    if(c == 3)
	      object->view_matrix = mat4_ortho(- 0.5 * fs[0], 0.5 * fs[0], -0.5 * fs[1], 0.5 * fs[1], -0.5 * fs[2], 0.5 * fs[2]);  
	  }
	  dealloc(fs);
	}

      }
      ptr = pair_cdr(ptr); 
    }

    
    return sc->NIL;
  }
  
  if(!get_model_id_from_pair(ptr, &id)){
    logd("Error unable to config model\n");
    return sc->NIL;
  }
  ptr = pair_cdr(args);
  while(ptr != sc->NIL){

    pointer pt2 = pair_car(ptr); //(poly 1 2 3)
    pointer sym = pair_car(pt2);
    if(is_symbol(sym)){
      char * symchars = symname(sym);

      bool ispoly = strcmp(symchars, "poly") == 0;
      bool iscolor = !ispoly && strcmp(symchars, "color") == 0;
      bool isscale = !iscolor && !ispoly && strcmp(symchars, "scale") == 0;
      bool isrotate = !iscolor && !ispoly && strcmp(symchars, "rotate") == 0;
      bool isoffset = strcmp(symchars, "offset") == 0;
      bool ismodel = strcmp(symchars, "model") == 0;
      bool istexture = strcmp(symchars, "texture") == 0;
      bool istext = !istexture && strcmp(symchars, "text") == 0;
      bool isview = strcmp(symchars, "view") == 0;
      bool isdf = strcmp(symchars, "distance-field") == 0;

      bool isuv = strcmp(symchars, "uv") == 0;
      if(ispoly || iscolor || isscale || isoffset || isrotate || isuv){
	// this is for model data that is only floats.
	size_t c = 0;
	f32 * fs = pointer_to_floats(sc, pair_cdr(pt2), &c);
	var object =  current_context->models + id;

	if(ispoly){
	  object->type = MODEL_TYPE_POLYGONAL;
	  if(object->verts == NULL)
	    object->verts = blit3d_polygon_new();
	  blit3d_polygon_load_data(object->verts, fs, c * sizeof(f32));
	  blit3d_polygon_configure(object->verts, 3);
	}else if (isuv){
	  if(object->uvs == NULL)
	    object->uvs = blit3d_polygon_new();
	  blit3d_polygon_load_data(object->uvs, fs, c * sizeof(f32));
	  blit3d_polygon_configure(object->uvs, 2);

	}else if(iscolor){
	  memcpy(object->color.data, fs, MIN(4, c) * sizeof(fs[0]));
	  object->mode |= MODEL_MODE_COLOR;
	  //printf("COLOR: ");vec4_print(object->color);printf("\n");
	}else if(isscale){
	  memcpy(object->scale.data, fs, MIN(3, c) * sizeof(fs[0]));
	  //printf("SCALE: ");vec3_print(object->scale);printf("\n");
	}else if(isoffset){
	  memcpy(object->offset.data, fs, MIN(3, c) * sizeof(fs[0]));
	  //printf("OFFSET: ");vec3_print(object->scale);printf("\n");
	}else if(isrotate){
	  memcpy(object->rotation.data, fs, MIN(3, c) * sizeof(fs[0]));
	  //printf("ROTATE: ");vec3_print(object->rotation);printf("\n");
	}
	free(fs);
      }

      
      var object =  current_context->models + id;
      if(isdf){
	var sub = pair_cdr(pt2);
	distance_field * prev = NULL;
	while(sub != sc->NIL){
	  ASSERT(prev == NULL);
	  prev = distance_field_load(sc, pair_car(sub));
	  logd("distance_field_load\n");
	  sub = pair_cdr(sub);
	}
	logd("loade %i\n", prev);
	object->distance_field = prev;

      }
      if(ismodel){
	u32 sub_id;
	if(get_model_id_from_pair(pt2, &sub_id)){
	  create_sub_model(id, sub_id);
	  //printf("creating sub model: %i -> %i\n", id, sub_id);
	}else{
	  printf("error creating sub model\n");
	}
      }
      if(isview){
	u32 sub_id;
	if(get_view_id_from_pair(pt2, &sub_id))
	  object->view_id = sub_id;
      }

      
      if(istexture){
	const char * str = sc->vptr->string_value(pair_cdr(pt2));
	if(str != NULL)
	  object->cache.path = iron_clone(str, strlen(str) + 1);
      }
      if(istext){
	const char * str = sc->vptr->string_value(pair_cdr(pt2));
	var object =  current_context->models + id;
	object->type = MODEL_TYPE_TEXT;
	object->text = iron_clone(str, strlen(str) + 1);
	
      }

    }
    ptr = pair_cdr(ptr); 
  }
  //ASSERT(is_list(ptr));
  //printf("Config model! %i\n", id);
  return sc->NIL;
}

pointer object_id_new(scheme * sc, pointer args){
  u32 objectid = model_new();  
  return mk_integer(sc, (long)objectid);
}

pointer sc_view_new(scheme * sc, pointer args){
  u32 objectid = view_new();  
  return mk_integer(sc, (long) objectid);
}

pointer pop_events(scheme * sc, pointer args){
  
  pointer new = pair_car(current_context->events);
  set_car(current_context->events, sc->NIL);
  return new;
  //return POP(current_context->events, sc->NIL);
}

void push_key_event(context * ctx, int key){
  var sc = ctx->sc;
  pointer s = NULL;
  if(s == NULL)
    s = mk_symbol(sc, "key");
  logd("%p\n", s);
  var key_val = mk_integer(sc, key);
  var item =  key_val;//_cons(sc, key_val, 1);
  set_car(ctx->events, _cons(sc, _cons(sc, s, key_val, 1), pair_car(ctx->events), 1));

}



pointer pair_cadr(pointer c){
  return pair_car(pair_cdr(c));
}

pointer sub_models(scheme * sc, pointer args){
  pointer a1 = pair_car(args);
  pointer pt2 = pair_car(a1); //(poly 1 2 3)
  pointer num = pair_cdr(a1);
  ASSERT(is_symbol(pt2));
  ASSERT(is_integer(num));
  i32 id = ivalue(num);
  
  pointer sym = pair_car(pt2);
    
  var ctx = current_context;
  size_t indexes[10];
  size_t cnt;
  size_t index = 0;
  pointer lst = sc->NIL;
  pointer model = mk_symbol(sc, "model");
  int keyid = ivalue(pair_car(args));
  while((cnt = u32_to_u32_table_iter(ctx->model_to_sub_model, &id, 1, NULL, indexes, array_count(indexes), &index))){
    for(size_t i = 0 ; i < cnt; i++){
      var sub = ctx->model_to_sub_model->value[indexes[i]];
      lst = _cons(sc, _cons(sc, model, mk_integer(sc, sub), 1), lst, 1);
    }
  }
  return lst;
}

u32 sc_to_model(scheme * sc, pointer obj){
  pointer sym = pair_car(obj);
  pointer model = mk_symbol(sc, "model");
  ASSERT(model == sym);
  pointer id = pair_cdr(obj);
  ASSERT(is_integer(id));
  return (u32)ivalue(id);
}

void trig_min_max(f32 * v, vec3 * min, vec3 * max, u32 vert_count){
  *min = vec3_infinity;
  *max = vec3_negative_infinity;
  for(u32 i =0; i < count; i++){
    var v2 = vec3_new(v[0], v[1], v[2]);
    *min = vec3_min(v2, *min);
    *max = vec3_max(v2, *max);
    v+= 3;
  }
}

bool detect_collision2(context * ctx, u32 model1, u32 model2, mat4 m1, mat4 m2){

  var obj1 = ctx->models + model1;
  var obj2 = ctx->models + model2;
  var t1 = get_object_transform(obj1);
  m1 = mat4_mul(m1, t1);
  if(obj1->distance_field != NULL && obj2->distance_field != NULL){
    //logd("check CD collision %i %i %f!\n", model1, model2);
    f32 d;
    vec3 pt = trace_closest_point(vec3_new(10, 10, 10) , m1, m2, obj1->distance_field, obj2->distance_field, &d);
    if(d < 0.1){
      return true;
    }
    
  }
  size_t indexes[10];
  size_t cnt;
  size_t index = 0;
  while((cnt = u32_to_u32_table_iter(ctx->model_to_sub_model, &model1, 1, NULL, indexes, array_count(indexes), &index))){
    for(size_t i1 = 0; i1 < cnt; i1++){
      var sub = ctx->model_to_sub_model->value[indexes[i1]];
      if(detect_collision2(ctx, sub, model2, m1, m2)){
	return true;
      }
    }
  }
  return false;
  
}

bool detect_collision(context * ctx, u32 model1, u32 model2, mat4 m1, mat4 m2){

  var obj1 = ctx->models + model1;
  var t1 = get_object_transform(obj1);

  m1 = mat4_mul(m1, t1);
  if(detect_collision2(ctx, model2, model1, m2, m1)){
    return true;
  }

  
  /*
  if(obj1->verts != NULL && obj2->verts != NULL && obj1->verts){
    u32 vert1_len = 0, vert2_len = 0;
    f32 * vert1 = blit3d_polygon_get_verts(obj1->verts, &vert1_len);
    f32 * vert2 = blit3d_polygon_get_verts(obj2->verts, &vert2_len);
    if(vert1 != NULL && vert2 != NULL){
      
      
    }
    }*/
  
  
  // recurse..
  size_t indexes[10];
  size_t cnt;
  size_t index = 0;
  while((cnt = u32_to_u32_table_iter(ctx->model_to_sub_model, &model1, 1, NULL, indexes, array_count(indexes), &index))){
    for(size_t i1 = 0; i1 < cnt; i1++){
      var sub = ctx->model_to_sub_model->value[indexes[i1]];
      if(detect_collision(ctx, sub, model2, m1, m2))
	return true;

      
      /*size_t indexes2[10];
      size_t cnt2;
      size_t index2 = 0;
      while((cnt2 = u32_to_u32_table_iter(ctx->model_to_sub_model, &model2, 1, NULL, indexes2, array_count(indexes2), &index2))){
	for(size_t i2 = 0; i2 < cnt; i2++){
	  var sub2 = ctx->model_to_sub_model->value[indexes2[i1]];
	  logd("Sub> %i %i\n", sub, sub2);
	}
	}*/
    }
  }
  return false;
}


pointer sc_detect_collisions(scheme * sc, pointer args){
  pointer a1 = pair_car(args);
  pointer a2 = pair_car(pair_cdr(args));
  pointer result = sc->NIL;
  while(sc->NIL != a1){
    pointer item = pair_car(a1);
    pointer it = a2;
    u32 model1 = sc_to_model(sc, item);
    while(sc->NIL != it){

      pointer item2 = pair_car(it);
      u32 model2 = sc_to_model(sc, item2);
      
      if(detect_collision(current_context, model1, model2, mat4_identity(), mat4_identity())){
	result = _cons(sc,_cons(sc,item, _cons(sc, item2, sc->NIL,1), 1), result, 1);
      }
      it = pair_cdr(it);
    }

    a1 = pair_cdr(a1);
  }

  return result;
}

pointer sc_die(scheme * sc, pointer args){
  exit(1);
  return sc->NIL;
}

pointer vec3_to_cons(scheme * sc, vec3 v){
  return _cons(sc, mk_real(sc, v.x),
	       _cons(sc, mk_real(sc, v.y),
		     _cons(sc, mk_real(sc, v.z), sc->NIL, 1),
		     1),
	      1);
		   
}

pointer model_offset(scheme * sc, pointer args){
  u32 model_id = sc_to_model(sc, pair_car(args));
  var ctx = current_context;
  vec3 offset = ctx->models[model_id].offset;
  
  return vec3_to_cons(sc, offset);
}

pointer scheme_init_internal(scheme * sc, pointer args){
  pointer l = pair_car(args);
  current_context->events = l;
  return sc->NIL;
}


extern unsigned char _usr_share_fonts_truetype_dejavu_DejaVuSans_ttf[];
context * context_init(gl_window * win){
  font * fnt =  blit_load_font_from_buffer(_usr_share_fonts_truetype_dejavu_DejaVuSans_ttf, 70);
  printf("Loaded font: %p\n", fnt);
  blit_set_current_font(fnt);
  

  static scheme_registerable reg[] = {
				      {.f = print_result, .name = "print2"},
				      
				      {.f = set_bg_color, .name = "set-bg-color"},
				      {.f = show_model, .name = "show-model"},
				      {.f = unshow_model, .name = "unshow-model"},
				      {.f = set_perspective, .name="perspective"},
				      {.f = set_orthographic, .name="orthographic"},
				      {.f = set_camera, .name="set-camera"},
				      {.f = key_is_down, .name="key-is-down"},
				      {.f = camera_direction, .name="camera-direction"},
				      {.f = camera_right, .name="camera-right"},
				      {.f = get_dt, .name="get-dt"}
				      ,{.f = config_model, .name="config-model"}
				      ,{.f = object_id_new, .name = "object-new"}
				      ,{.f = sc_view_new, .name = "view-new"}
				      ,{.f = pop_events, .name = "pop-events"},
				      {.f = sub_models, .name = "sub-models"},
				      {.f = sc_detect_collisions, .name = "detect-collisions"},
				      {.f = sc_die, .name= "shutdown"},
				      {.f = model_offset, .name = "model-offset"},
				      {.f = scheme_init_internal, .name = "__scheme-init-internal__"}
  };

    context * ctx = alloc0(sizeof(ctx[0]));
   ctx->blit3d  = blit3d_context_new();

   var sc = scheme_init_new();
   ctx->sc = sc;
   ctx->events = sc->NIL;

   
   scheme_set_output_port_file(sc, stdout);
   scheme_register_foreign_func_list(sc, reg, array_count(reg));

   current_context = ctx;
   context_load_lisp_string2(ctx, "(define **events** (cons () ())) (__scheme-init-internal__ **events**)");
   ctx->shown_models = u32_to_u32_table_create(NULL);
   ctx->model_to_sub_model = u32_to_u32_table_create(NULL);
   ((bool *) &ctx->model_to_sub_model->is_multi_table)[0] = true;
   ctx->running = 1;
   ctx->win = win;
   ctx->view_matrix = mat4_identity();
   ctx->camera_matrix = mat4_identity();
   init_audio_subsystem(ctx);


   // opengl must be initialized at this point!
   
   return ctx;
}


void context_load_lisp(context * ctx, const char * file){
  printf("Load file: %s\n", file);
  u32_to_u32_table_clear(ctx->shown_models);
  u32_to_u32_table_clear(ctx->model_to_sub_model);
  u32_to_u32_table_clear(ctx->model_to_sub_model);
  ctx->model_count = 0;
  ctx->view_count = 0;
  var fin = fopen(file, "r");
  scheme_load_named_file(ctx->sc, fin, file);
  fclose(fin);
  if(ctx->sc->retcode)
    exit(0);
}

void context_load_lisp_string(context * ctx, const char * string, size_t length){
  char * str = alloc0(length + 1);
  memcpy(str, string, length);
  scheme_load_string(ctx->sc, str);
  dealloc(str);
}
void context_load_lisp_string2(context * ctx, const char * string){
  context_load_lisp_string(ctx, string, strlen(string));
}

