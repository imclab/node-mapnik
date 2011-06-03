
#include <node_buffer.h>
#include <node_version.h>

// mapnik
#include <mapnik/version.hpp>
#include <mapnik/map.hpp>
#include <mapnik/projection.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/filter_factory.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/config_error.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/save_map.hpp>
#include <mapnik/query.hpp>
#include <mapnik/ctrans.hpp>
// provides MAPNIK_SUPPORTS_GRID_RENDERER
#include <mapnik/config.hpp>

// mapnik renderers
#include <mapnik/agg_renderer.hpp>
#if defined(MAPNIK_SUPPORTS_GRID_RENDERER)
#include <mapnik/grid/grid_renderer.hpp>
#endif
#if defined(HAVE_CAIRO)
#include <mapnik/cairo_renderer.hpp>
#endif

// stl
#include <exception>
#include <set>

// boost
#include <boost/foreach.hpp>

#include "utils.hpp"
#include "js_grid_utils.hpp"
#include "mapnik_map.hpp"
#include "ds_emitter.hpp"
#include "layer_emitter.hpp"
#include "mapnik_layer.hpp"

Persistent<FunctionTemplate> Map::constructor;

void Map::Initialize(Handle<Object> target) {

    HandleScope scope;

    constructor = Persistent<FunctionTemplate>::New(FunctionTemplate::New(Map::New));
    constructor->InstanceTemplate()->SetInternalFieldCount(1);
    constructor->SetClassName(String::NewSymbol("Map"));

    NODE_SET_PROTOTYPE_METHOD(constructor, "load", load);
    NODE_SET_PROTOTYPE_METHOD(constructor, "save", save);
    NODE_SET_PROTOTYPE_METHOD(constructor, "clear", clear);
    NODE_SET_PROTOTYPE_METHOD(constructor, "from_string", from_string);
    NODE_SET_PROTOTYPE_METHOD(constructor, "toXML", to_string);
    NODE_SET_PROTOTYPE_METHOD(constructor, "resize", resize);
    NODE_SET_PROTOTYPE_METHOD(constructor, "width", width);
    NODE_SET_PROTOTYPE_METHOD(constructor, "height", height);
    NODE_SET_PROTOTYPE_METHOD(constructor, "buffer_size", buffer_size);
#if defined(MAPNIK_SUPPORTS_GRID_RENDERER)
    NODE_SET_PROTOTYPE_METHOD(constructor, "render_grid", render_grid);
#endif
    NODE_SET_PROTOTYPE_METHOD(constructor, "extent", extent);
    NODE_SET_PROTOTYPE_METHOD(constructor, "zoom_all", zoom_all);
    NODE_SET_PROTOTYPE_METHOD(constructor, "zoom_to_box", zoom_to_box);
    NODE_SET_PROTOTYPE_METHOD(constructor, "render", render);
    NODE_SET_PROTOTYPE_METHOD(constructor, "render_to_string", render_to_string);
    NODE_SET_PROTOTYPE_METHOD(constructor, "render_to_file", render_to_file);
    NODE_SET_PROTOTYPE_METHOD(constructor, "scaleDenominator", scale_denominator);

    // layer access
    NODE_SET_PROTOTYPE_METHOD(constructor, "add_layer", add_layer);
    NODE_SET_PROTOTYPE_METHOD(constructor, "get_layer", get_layer);

    // temp hack to expose layer metadata
    NODE_SET_PROTOTYPE_METHOD(constructor, "layers", layers);
    NODE_SET_PROTOTYPE_METHOD(constructor, "features", features);
    NODE_SET_PROTOTYPE_METHOD(constructor, "describe_data", describe_data);

    // properties
    ATTR(constructor, "srs", get_prop, set_prop);

    target->Set(String::NewSymbol("Map"),constructor->GetFunction());
    //eio_set_max_poll_reqs(10);
    //eio_set_min_parallel(10);
}

Map::Map(int width, int height) :
  ObjectWrap(),
  map_(new mapnik::Map(width,height)),
  in_use_(0) {}

Map::Map(int width, int height, std::string const& srs) :
  ObjectWrap(),
  map_(new mapnik::Map(width,height,srs)),
  in_use_(0) {}

Map::~Map()
{
    // std::clog << "~Map(node)\n";
    // release is handled by boost::shared_ptr
}

void Map::acquire() {
    //std::cerr << "acquiring!!\n";
    ++in_use_;
}

void Map::release() {
    //std::cerr << "releasing!!\n";
    --in_use_;
}

int Map::active() const {
    return in_use_;
}

Handle<Value> Map::New(const Arguments& args)
{
    HandleScope scope;

    if (!args.IsConstructCall())
        return ThrowException(String::New("Cannot call constructor as function, you need to use 'new' keyword"));

    // accept a reference or v8:External?
    if (args[0]->IsExternal())
    {
        return ThrowException(String::New("No support yet for passing v8:External wrapper around C++ void*"));
    }

    if (args.Length() == 2)
    {
        if (!args[0]->IsNumber() || !args[1]->IsNumber())
            return ThrowException(Exception::Error(
               String::New("'width' and 'height' must be a integers")));
        Map* m = new Map(args[0]->IntegerValue(),args[1]->IntegerValue());
        m->Wrap(args.This());
        return args.This();
    }
    else if (args.Length() == 3)
    {
        if (!args[0]->IsNumber() || !args[1]->IsNumber())
            return ThrowException(Exception::Error(
               String::New("'width' and 'height' must be a integers")));
        if (!args[2]->IsString())
            return ThrowException(Exception::Error(
               String::New("'srs' value must be a string")));            
        Map* m = new Map(args[0]->IntegerValue(),args[1]->IntegerValue(),TOSTR(args[2]));
        m->Wrap(args.This());
        return args.This();
    }
    else
    {
        return ThrowException(Exception::Error(
          String::New("please provide Map width and height and optional srs")));
    }
    //return args.This();
    return Undefined();
}

Handle<Value> Map::get_prop(Local<String> property,
                         const AccessorInfo& info)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(info.This());
    std::string a = TOSTR(property);
    if (a == "srs")
        return scope.Close(String::New(m->map_->srs().c_str()));
    return Undefined();
}

void Map::set_prop(Local<String> property,
                         Local<Value> value,
                         const AccessorInfo& info)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(info.Holder());
    std::string a = TOSTR(property);
    if (a == "srs")
    {
        if (!value->IsString()) {
            ThrowException(Exception::Error(
               String::New("'srs' must be a string")));
        } else {
            m->map_->set_srs(TOSTR(value));
        }
    }

}

Handle<Value> Map::add_layer(const Arguments &args) {
    HandleScope scope;
    Local<Object> obj = args[0]->ToObject();
    if (args[0]->IsNull() || args[0]->IsUndefined() || !Layer::constructor->HasInstance(obj))
      return ThrowException(Exception::TypeError(String::New("mapnik.Layer expected")));
    Layer *l = ObjectWrap::Unwrap<Layer>(obj);
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    // TODO - addLayer should be add_layer in mapnik
    m->map_->addLayer(*l->get());
    return Undefined();
}

Handle<Value> Map::get_layer(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() == 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer name or index")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::vector<mapnik::layer> & layers = m->map_->layers();

    Local<Value> layer = args[0];
    if (layer->IsNumber())
    {
        unsigned int index = args[0]->IntegerValue();

        if (index < layers.size())
        {
            return scope.Close(Layer::New(layers[index]));
        }
        else
        {
          return ThrowException(Exception::TypeError(
            String::New("invalid layer index")));
        }    
    }
    else if (layer->IsString())
    {
        bool found = false;
        unsigned int idx(0);
        std::string const & layer_name = TOSTR(layer);
        BOOST_FOREACH ( mapnik::layer const& lyr, layers )
        {
            if (lyr.name() == layer_name)
            {
                found = true;
                return scope.Close(Layer::New(layers[idx]));
            }
            ++idx; 
        }
        if (!found) 
        {
            std::ostringstream s;
            s << "Layer name '" << layer_name << "' not found";
            return ThrowException(Exception::TypeError(
              String::New(s.str().c_str())));
        }
    
    }
    else
    {
        return ThrowException(Exception::TypeError(
           String::New("first argument must be either a layer name(string) or layer index (integer)")));
    }

    return Undefined();

}


Handle<Value> Map::layers(const Arguments& args)
{
    HandleScope scope;

    // todo - optimize by allowing indexing...
    /*if (!args.Length() == 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer index")));

    if (!args[0]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("layer index must be an integer")));
    */

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    std::vector<mapnik::layer> const & layers = m->map_->layers();
    Local<Array> a = Array::New(layers.size());

    for (unsigned i = 0; i < layers.size(); ++i )
    {
        const mapnik::layer & layer = layers[i];
        Local<Object> meta = Object::New();
        layer_as_json(meta,layer);
        a->Set(i, meta);
    }

    return scope.Close(a);

}

Handle<Value> Map::describe_data(const Arguments& args)
{
    HandleScope scope;

    // todo - optimize by allowing indexing...
    /*if (!args.Length() == 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer index")));

    if (!args[0]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("layer index must be an integer")));
    */

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    std::vector<mapnik::layer> const & layers = m->map_->layers();

    Local<Object> meta = Object::New();

    for (unsigned i = 0; i < layers.size(); ++i )
    {
        const mapnik::layer & layer = layers[i];
        Local<Object> description = Object::New();
        mapnik::datasource_ptr ds = layer.datasource();
        if (ds)
        {
            describe_datasource(description,ds);
        }
        meta->Set(String::NewSymbol(layer.name().c_str()), description);
    }

    return scope.Close(meta);

}


Handle<Value> Map::features(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer index")));

    if (!args[0]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("layer index must be an integer")));

    unsigned first = 0;
    unsigned last = 0;

    // we are slicing
    if (args.Length() == 3)
    {
        if (!args[1]->IsNumber() || !args[2]->IsNumber())
            return ThrowException(Exception::Error(
               String::New("Index of 'first' and 'last' feature must be an integer")));
        first = args[1]->IntegerValue();
        last = args[2]->IntegerValue();
    }

    unsigned index = args[0]->IntegerValue();

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    std::vector<mapnik::layer> const & layers = m->map_->layers();

    // TODO - we don't know features.length at this point
    Local<Array> a = Array::New(0);
    if ( index < layers.size())
    {
        mapnik::layer const& layer = layers[index];
        mapnik::datasource_ptr ds = layer.datasource();
        if (ds)
        {
            datasource_features(a,ds,first,last);
        }
    }

    return scope.Close(a);

}

Handle<Value> Map::clear(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    m->map_->remove_all();
    return Undefined();
}

Handle<Value> Map::resize(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() == 2)
      return ThrowException(Exception::Error(
        String::New("Please provide width and height")));

    if (!args[0]->IsNumber() || !args[1]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("width and height must be integers")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    m->map_->resize(args[0]->IntegerValue(),args[1]->IntegerValue());
    return Undefined();
}


Handle<Value> Map::width(const Arguments& args)
{
    HandleScope scope;
    if (!args.Length() == 0)
      return ThrowException(Exception::Error(
        String::New("accepts no arguments")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    Local<Value> width = Integer::New(m->map_->width());
    return scope.Close(width);
}

Handle<Value> Map::height(const Arguments& args)
{
    HandleScope scope;
    if (!args.Length() == 0)
      return ThrowException(Exception::Error(
        String::New("accepts no arguments")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    Local<Value> width = Integer::New(m->map_->height());
    return scope.Close(width);
}

Handle<Value> Map::buffer_size(const Arguments& args)
{
    HandleScope scope;
    if (!args.Length() == 1)
      return ThrowException(Exception::Error(
        String::New("Please provide a buffer_size")));

    if (!args[0]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("buffer_size must be an integer")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    m->map_->set_buffer_size(args[0]->IntegerValue());
    return Undefined();
}

Handle<Value> Map::load(const Arguments& args)
{
    HandleScope scope;
    if (args.Length() != 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a path to a mapnik stylesheet")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string const& stylesheet = TOSTR(args[0]);
    bool strict = false;
    try
    {
        mapnik::load_map(*m->map_,stylesheet,strict);
    }
    catch (const mapnik::config_error & ex )
    {
      return ThrowException(Exception::Error(
        String::New(ex.what())));
    }
    catch (...)
    {
      return ThrowException(Exception::TypeError(
        String::New("something went wrong loading the map")));
    }
    return Undefined();
}

Handle<Value> Map::save(const Arguments& args)
{
    HandleScope scope;
    if (args.Length() != 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a path to map.xml to save")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string const& filename = TOSTR(args[0]);
    bool explicit_defaults = false;
    mapnik::save_map(*m->map_,filename,explicit_defaults);
    return Undefined();
}

Handle<Value> Map::from_string(const Arguments& args)
{
    HandleScope scope;
    if (!args.Length() >= 1) {
        return ThrowException(Exception::TypeError(
        String::New("Accepts 2 arguments: map string and optional base_path")));
    }

    if (!args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a mapnik stylesheet string")));


    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string const& stylesheet = TOSTR(args[0]);
    bool strict = false;

    std::string base_path = "";
    
    if (args.Length() >= 1 && args[1]->IsString()) {
        base_path = TOSTR(args[1]);
    }

    try
    {
        mapnik::load_map_string(*m->map_,stylesheet,strict,base_path);
    }
    catch (const mapnik::config_error & ex )
    {
      return ThrowException(Exception::Error(
        String::New(ex.what())));
    }
    catch (...)
    {
      return ThrowException(Exception::TypeError(
        String::New("something went wrong loading the map")));
    }
    return Undefined();
}

Handle<Value> Map::to_string(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    bool explicit_defaults = false;
    std::string map_string = mapnik::save_map_to_string(*m->map_,explicit_defaults);
    return scope.Close(String::New(map_string.c_str()));
}

Handle<Value> Map::scale_denominator(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    return scope.Close(Number::New(m->map_->scale_denominator()));
}

Handle<Value> Map::extent(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    Local<Array> a = Array::New(4);
    mapnik::box2d<double> const& e = m->map_->get_current_extent();
    a->Set(0, Number::New(e.minx()));
    a->Set(1, Number::New(e.miny()));
    a->Set(2, Number::New(e.maxx()));
    a->Set(3, Number::New(e.maxy()));
    return scope.Close(a);
}

Handle<Value> Map::zoom_all(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    try {
      m->map_->zoom_all();
    }
    catch (const mapnik::config_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::datasource_exception & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::proj_init_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const std::runtime_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (std::exception & ex)
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (...)
    {
        return ThrowException(Exception::TypeError(
          String::New("Unknown exception happened while zooming, please submit a bug report")));
    }
    return Undefined();
}

Handle<Value> Map::zoom_to_box(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    double minx;
    double miny;
    double maxx;
    double maxy;

    if (args.Length() == 1)
    {
        if (!args[0]->IsArray())
            return ThrowException(Exception::Error(
               String::New("Must provide an array of: [minx,miny,maxx,maxy]")));
        Local<Array> a = Local<Array>::Cast(args[0]);
        minx = a->Get(0)->NumberValue();
        miny = a->Get(1)->NumberValue();
        maxx = a->Get(2)->NumberValue();
        maxy = a->Get(3)->NumberValue();

    }
    else if (args.Length() != 4)
      return ThrowException(Exception::Error(
        String::New("Must provide 4 arguments: minx,miny,maxx,maxy")));
    else {
        minx = args[0]->NumberValue();
        miny = args[1]->NumberValue();
        maxx = args[2]->NumberValue();
        maxy = args[3]->NumberValue();
    }
    mapnik::box2d<double> box(minx,miny,maxx,maxy);
    m->map_->zoom_to_box(box);
    return Undefined();
}

typedef struct {
    Map *m;
    std::string format;
    mapnik::box2d<double> bbox;
    bool error;
    std::string error_name;
    std::string im_string;
    Persistent<Function> cb;
} closure_t;

Handle<Value> Map::render(const Arguments& args)
{
    HandleScope scope;
    
    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    if (m->active() != 0) {
        std::ostringstream s;
        s << "render: this map appears to be in use by "
          << m->active()
          << " other thread(s) which is not allowed."
          << " You need to use a map pool to avoid sharing map objects between concurrent rendering";
        std::cerr << s.str() << "\n";
        //return ThrowException(Exception::Error(
          //String::New(s.str().c_str())));
    }

    /*
    std::clog << "eio_nreqs" << eio_nreqs() << "\n";
    std::clog << "eio_nready" << eio_nready() << "\n";
    std::clog << "eio_npending" << eio_npending() << "\n";
    std::clog << "eio_nthreads" << eio_nthreads() << "\n";
    */

    if (args.Length() < 3)
        return ThrowException(Exception::TypeError(
          String::New("requires three arguments, a extent array, a format, and a callback")));

    // extent array
    if (!args[0]->IsArray())
        return ThrowException(Exception::TypeError(
           String::New("first argument must be an extent array of: [minx,miny,maxx,maxy]")));

    // format
    if (!args[1]->IsString())
        return ThrowException(Exception::TypeError(
           String::New("second argument must be an format string")));

    // function callback
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                  String::New("last argument must be a callback function")));

    Local<Array> a = Local<Array>::Cast(args[0]);
    uint32_t a_length = a->Length();
    if (!a_length  == 4) {
        return ThrowException(Exception::TypeError(
           String::New("first argument must be 4 item array of: [minx,miny,maxx,maxy]")));
    }

    closure_t *closure = new closure_t();

    if (!closure) {
      V8::LowMemoryNotification();
      return ThrowException(Exception::Error(
            String::New("Could not allocate enough memory")));
    }

    double minx = a->Get(0)->NumberValue();
    double miny = a->Get(1)->NumberValue();
    double maxx = a->Get(2)->NumberValue();
    double maxy = a->Get(3)->NumberValue();


    closure->m = m;
    closure->format = TOSTR(args[1]);
    closure->error = false;
    closure->bbox = mapnik::box2d<double>(minx,miny,maxx,maxy);
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(args[args.Length()-1]));
    eio_custom(EIO_Render, EIO_PRI_DEFAULT, EIO_AfterRender, closure);
    ev_ref(EV_DEFAULT_UC);
    m->acquire();
    m->Ref();
    return Undefined();
}

int Map::EIO_Render(eio_req *req)
{
    closure_t *closure = static_cast<closure_t *>(req->data);

    // zoom to
    closure->m->map_->zoom_to_box(closure->bbox);
    try
    {
        mapnik::image_32 im(closure->m->map_->width(),closure->m->map_->height());
        mapnik::agg_renderer<mapnik::image_32> ren(*closure->m->map_,im);
        ren.apply();
        closure->im_string = save_to_string(im, closure->format);
    }
    catch (const mapnik::config_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::datasource_exception & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::proj_init_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::runtime_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "unknown exception happened while rendering the map,\n this should not happen, please submit a bug report";
    }
    return 0;
}

int Map::EIO_AfterRender(eio_req *req)
{
    HandleScope scope;

    closure_t *closure = static_cast<closure_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);

    TryCatch try_catch;

    if (closure->error) {
        // TODO - add more attributes
        // https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/Error
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {
        #if NODE_VERSION_AT_LEAST(0,3,0)
          node::Buffer *retbuf = Buffer::New((char *)closure->im_string.data(),closure->im_string.size());
        #else
          node::Buffer *retbuf = Buffer::New(closure->im_string.size());
          memcpy(retbuf->data(), closure->im_string.data(), closure->im_string.size());
        #endif
        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(retbuf->handle_) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    closure->m->release();
    closure->m->Unref();
    closure->cb.Dispose();
    delete closure;
    return 0;
}

Handle<Value> Map::render_to_string(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("argument must be a format string")));

    std::string format = TOSTR(args[0]);

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string s;
    try
    {
        mapnik::image_32 im(m->map_->width(),m->map_->height());
        mapnik::agg_renderer<mapnik::image_32> ren(*m->map_,im);
        ren.apply();
        //std::string ss = mapnik::save_to_string<mapnik::image_data_32>(im.data(),"png");
        s = save_to_string(im, format);

    }
    catch (const mapnik::config_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::datasource_exception & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::proj_init_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const std::runtime_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (std::exception & ex)
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (...)
    {
        return ThrowException(Exception::TypeError(
          String::New("unknown exception happened while rendering the map, please submit a bug report")));
    }

    #if NODE_VERSION_AT_LEAST(0,3,0)
      node::Buffer *retbuf = Buffer::New((char*)s.data(),s.size());
    #else
      node::Buffer *retbuf = Buffer::New(s.size());
      memcpy(retbuf->data(), s.data(), s.size());
    #endif

    return scope.Close(retbuf->handle_);
}

Handle<Value> Map::render_to_file(const Arguments& args)
{
    HandleScope scope;
    if (!args.Length() >= 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a path to a file to save")));

    if (args.Length() > 2)
      return ThrowException(Exception::TypeError(
        String::New("accepts two arguments, a required path to a file, and an optional options object, eg. {format: 'pdf'}")));

    std::string format("");

    if (args.Length() == 2){
      if (!args[1]->IsObject())
        return ThrowException(Exception::TypeError(
          String::New("second argument is optional, but if provided must be an object, eg. {format: 'pdf'}")));

        Local<Object> options = args[1]->ToObject();
        if (options->Has(String::New("format")))
        {
            Local<Value> format_opt = options->Get(String::New("format"));
            if (!format_opt->IsString())
              return ThrowException(Exception::TypeError(
                String::New("'format' must be a String")));

            format = TOSTR(format_opt);
        }
    }

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string const& output = TOSTR(args[0]);

    if (format.empty()) {
        format = mapnik::guess_type(output);
        if (format == "<unknown>") {
            std::ostringstream s("");
            s << "unknown output extension for: " << output << "\n";
            return ThrowException(Exception::Error(
                String::New(s.str().c_str())));
        }
    }

    try
    {

        if (format == "pdf" || format == "svg" || format =="ps" || format == "ARGB32" || format == "RGB24")
        {
    #if defined(HAVE_CAIRO)
            mapnik::save_to_cairo_file(*m->map_,output,format);
    #else
            std::ostringstream s("");
            s << "Cairo backend is not available, cannot write to " << format << "\n";
            return ThrowException(Exception::Error(
              String::New(s.str().c_str())));
    #endif
        }
        else
        {
            mapnik::image_32 im(m->map_->width(),m->map_->height());
            mapnik::agg_renderer<mapnik::image_32> ren(*m->map_,im);
            ren.apply();
            mapnik::save_to_file<mapnik::image_data_32>(im.data(),output);
        }
    }
    catch (const mapnik::config_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::datasource_exception & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::proj_init_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const std::runtime_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (std::exception & ex)
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (...)
    {
        return ThrowException(Exception::TypeError(
          String::New("unknown exception happened while rendering the map, please submit a bug report")));
    }
    return Undefined();
}

#if defined(MAPNIK_SUPPORTS_GRID_RENDERER)

struct grid_t {
    Map *m;
    boost::shared_ptr<mapnik::grid> grid_ptr;
    std::size_t layer_idx;
    std::string layer_name;
    std::string join_field;
    uint32_t num_fields;
    int size;
    bool error;
    std::string error_name;
    bool include_features;
    Persistent<Function> cb;
    bool grid_initialized;
};

Handle<Value> Map::render_grid(const Arguments& args)
{
    HandleScope scope;

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    if (m->active() != 0) {
        std::ostringstream s;
        s << "render_grid: this map appears to be in use by "
          << m->active()
          << " other thread(s) which is not allowed."
          << " You need to use a map pool to avoid sharing map objects between concurrent rendering";
        std::cerr << s.str() << "\n";
        //return ThrowException(Exception::Error(
        //  String::New(s.str().c_str())));
    }

    if (!args.Length() >= 2)
      return ThrowException(Exception::Error(
        String::New("please provide layer name or index, options, and callback")));

    // make sure layer name is a string
    Local<Value> layer = args[0];
    if (! (layer->IsString() || layer->IsNumber()) )
        return ThrowException(Exception::TypeError(
           String::New("first argument must be either a layer name(string) or layer index (integer)")));

    // ensure callback is a function
    Local<Value> callback = args[args.Length()-1];
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                  String::New("last argument must be a callback function")));

    // ensure options object
    if (!args[1]->IsObject())
        return ThrowException(Exception::TypeError(
          String::New("options must be an object, eg {key: '__id__', resolution : 4, fields: ['name']}")));
    
    Local<Object> options = args[1]->ToObject();

    std::string join_field("__id__");
    Local<String> param = String::New("key");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsString())
          return ThrowException(Exception::TypeError(
            String::New("'key' must be a string")));
        join_field = TOSTR(param_val);
    }

    unsigned int step(4);
    param = String::New("resolution");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsNumber())
          return ThrowException(Exception::TypeError(
            String::New("'resolution' must be an integer")));
        step = param_val->IntegerValue();
    }

    grid_t *closure = new grid_t();

    if (!closure) {
        V8::LowMemoryNotification();
        return ThrowException(Exception::Error(
            String::New("Could not allocate enough memory")));
    }

    if (layer->IsString()) {
        closure->layer_name = TOSTR(layer);
    } else if (layer->IsNumber()) {
        closure->layer_idx = static_cast<std::size_t>(layer->NumberValue());
    }

    closure->m = m;
    closure->join_field = join_field;
    closure->error = false;
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(callback));
    closure->num_fields = 0;
    
    unsigned int grid_width = m->map_->width()/step;
    unsigned int grid_height = m->map_->height()/step;

    closure->grid_ptr = boost::shared_ptr<mapnik::grid>(
                new mapnik::grid(grid_width,grid_height,closure->join_field,step)
            );

    param = String::New("fields");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsArray())
          return ThrowException(Exception::TypeError(
            String::New("'fields' must be an array of strings")));
        Local<Array> a = Local<Array>::Cast(param_val);
        uint32_t i = 0;
        closure->num_fields = a->Length();
        while (i < closure->num_fields) {
            Local<Value> name = a->Get(i);
            if (name->IsString()){
                closure->grid_ptr->add_property_name(TOSTR(name));
            }
            i++;
        }
    }

    eio_custom(EIO_RenderGrid, EIO_PRI_DEFAULT, EIO_AfterRenderGrid, closure);
    ev_ref(EV_DEFAULT_UC);
    m->acquire();
    m->Ref();
    return Undefined();

}


int Map::EIO_RenderGrid(eio_req *req)
{

    grid_t *closure = static_cast<grid_t *>(req->data);

    std::vector<mapnik::layer> const& layers = closure->m->map_->layers();
    
    if (!closure->layer_name.empty()) {
        bool found = false;
        unsigned int idx(0);
        std::string const & layer_name = closure->layer_name;
        BOOST_FOREACH ( mapnik::layer const& lyr, layers )
        {
            if (lyr.name() == layer_name)
            {
                found = true;
                closure->layer_idx = idx;
                break;
            }
            ++idx; 
        }
        if (!found) 
        {
            std::ostringstream s;
            s << "Layer name '" << layer_name << "' not found";
            closure->error = true;
            closure->error_name = s.str();
            return 0;
        }
    }
    else 
    {
        std::size_t layer_num = layers.size();
        std::size_t layer_idx = closure->layer_idx;
    
        if (layer_idx >= layer_num) {
            std::ostringstream s;
            s << "Zero-based layer index '" << layer_idx << "' not valid, only '"
              << layers.size() << "' layers are in map";
            closure->error = true;
            closure->error_name = s.str();
            return 0;
        }    
    }

    // copy property names
    std::set<std::string> attributes = closure->grid_ptr->property_names();

    std::string const& join_field = closure->join_field;
    
    if (join_field == closure->grid_ptr->id_name_) 
    {
        // TODO - should feature.id() be a first class attribute?
        if (attributes.find(join_field) != attributes.end())
        {
            attributes.erase(join_field);
        }
    }
    else if (attributes.find(join_field) == attributes.end())
    {
        attributes.insert(join_field);
    }

    try
    {
        mapnik::grid_renderer<mapnik::grid> ren(*closure->m->map_,*closure->grid_ptr,1.0,0,0);
        mapnik::layer const& layer = layers[closure->layer_idx];
        ren.apply(layer,attributes);
    }
    catch (const mapnik::config_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::datasource_exception & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::proj_init_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::runtime_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "Unknown error occured, please file bug";
    }

    return 0;

}


int Map::EIO_AfterRenderGrid(eio_req *req)
{
    HandleScope scope;

    grid_t *closure = static_cast<grid_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);

    TryCatch try_catch;

    if (closure->error) {
        // TODO - add more attributes
        // https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/Error
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {
        // convert buffer to utf and gather key order
        Local<Array> grid_array = Array::New();
        std::vector<mapnik::grid::lookup_type> key_order;
        node_mapnik::grid2utf(*closure->grid_ptr,grid_array,key_order);
    
        // convert key order to proper javascript array
        Local<Array> keys_a = Array::New(key_order.size());
        std::vector<std::string>::iterator it;
        unsigned int i;
        for (it = key_order.begin(), i = 0; it < key_order.end(); ++it, ++i)
        {
            keys_a->Set(i, String::New((*it).c_str()));
        }
    
        // gather feature data
        Local<Object> feature_data = Object::New();
        if (closure->num_fields > 0) {
            mapnik::grid::feature_type const& g_features = closure->grid_ptr->get_grid_features();
            node_mapnik::write_features(g_features,
                           feature_data,
                           key_order,
                           closure->join_field,
                           closure->grid_ptr->property_names());
        }
        
        // Create the return hash.
        Local<Object> json = Object::New();
        json->Set(String::NewSymbol("grid"), grid_array);
        json->Set(String::NewSymbol("keys"), keys_a);
        json->Set(String::NewSymbol("data"), feature_data);
        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(json) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    closure->m->release();
    closure->m->Unref();
    closure->cb.Dispose();
    delete closure;
    return 0;
}

#endif
