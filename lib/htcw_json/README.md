# JSON

A minimalist JSON parser with streaming support

```
[env:node32s]
platform = espressif32
board = node32s
framework = arduino
lib_deps = 
	codewitch-honey-crisis/htcw_json
```

See https://www.codeproject.com/Tips/5382125/IP-Geolocation-with-an-ESP32-and-Arduino for a simple real world example

```cpp
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <memory.h>
#include <string.h>
#include <json.hpp>
using namespace io;
using namespace json;
void indent(int spaces, FILE* file) {
    while(spaces--) fprintf(file,"    ");
}
// accept any reader regardless of capture size
void dump(json_reader_base& reader, FILE* file) {
    // don't de-escape and dequote field names or string values:
    //reader.raw_strings(true);
    
    bool first_part=true; // first value part in series
    int tabs = 0; // number of "tabs" to indent by
    bool skip_read = false; // don't call read() the next iteration
    while(skip_read || reader.read()) {
        skip_read = false;
        switch(reader.node_type()) {
            case json_node_type::array:
                indent(tabs++,file);
                fputs("[",file);
                break;
            case json_node_type::end_array:
                indent(--tabs,file);
                fputs("]",file);
                break;
            case json_node_type::object:
                indent(tabs++,file);
                fputs("{",file);
                break;
            case json_node_type::end_object:
                indent(--tabs,file);
                fputs("}",file);
                break;
            case json_node_type::field:
                indent(tabs,file);
                fprintf(file, "%s: ",reader.value());
                // we want to spit the value here, so 
                // we basically hijack the reader and 
                // read the value subtree here.
                while(reader.read() && reader.is_value()) {
                    fprintf(file,"%s",reader.value());
                }
                fputs("",file);
                skip_read = true;
                break;
            case json_node_type::value:
                indent(tabs,file);
                fprintf(file,"%s\r\n",reader.value());
                //fwrite(reader.value(),1,strlen(reader.value()),file);
                fputs("",file);
                break;
            case json_node_type::value_part:
                // the first value part needs to be indented
                if(first_part) {
                    indent(tabs,file);
                    first_part = false; // reset the flag
                }
                fprintf(file,"%s",reader.value());
                break;
            case json_node_type::end_value_part:
                fprintf(file,"%s,\r\n",reader.value());               
                // set the first flag
                first_part = true;
                break;      
        }
    }
}
char name[2048];
char overview[8192];
void read_episodes(json_reader_base& reader, FILE* file) {
    int episodes_array_depth = 0;
    if(reader.read() && reader.node_type()==json_node_type::array) {
        episodes_array_depth = reader.depth();
        while(true) {
            if(reader.depth()==episodes_array_depth && reader.node_type()==json_node_type::end_array) {
                break;
            }
            if(reader.read()&&reader.node_type()==json_node_type::object) {
                
                int episode_object_depth = reader.depth();
                int season_number = -1;
                int episode_number = -1;
                while(reader.read() && reader.depth()>=episode_object_depth) {
                    if(reader.depth()==episode_object_depth && reader.node_type()==json_node_type::field) {
                        if(0==strcmp("episode_number",reader.value()) && reader.read() && reader.node_type()==json_node_type::value) {
                            episode_number = reader.value_int();
                        }
                        if(0==strcmp("season_number",reader.value()) && reader.read() && reader.node_type()==json_node_type::value) {
                            season_number = reader.value_int();
                        }
                        if(0==strcmp("name",reader.value())) {
                            name[0]=0;
                            while(reader.read() && reader.is_value()) {
                                strcat(name,reader.value());
                            }
                        }
                        if(0==strcmp("overview",reader.value())) {
                            overview[0]=0;
                            while(reader.read() && reader.is_value()) {
                                strcat(overview,reader.value());
                            }
                        }
                    }
                }
                if(season_number>-1 && episode_number>-1 && name[0]) {
                    fprintf(file,"S%02dE%02d %s\r\n",season_number,episode_number,name);
                    if(overview[0]) {
                        fprintf(file,"\t%s\r\n",overview);
                    }
                    fputs("",file);
                }
            }
        }
    }
}
void read_series(json_reader_base& reader,FILE* file) {
    while(reader.read()) {
        switch(reader.node_type()) {
            case json_node_type::field:
                if(0==strcmp("episodes",reader.value())) {
                    read_episodes(reader,file);
                }
                break;
            default:
                break;      
        }
    }
}
int main() {
    file_stream stm("C:\\Users\\gazto\\Documents\\json_test\\data.json",io::file_mode::read);
    json_reader reader(stm);
    read_series(reader,stdout);
    return 0;
}
```

This will parse JSON of the following format

```js
{
  "backdrop_path": "/lgTB0XOd4UFixecZgwWrsR69AxY.jpg",
  "created_by": [
      {
        "id": 1233032,
        "credit_id": "525749f819c29531db09b231",
        "name": "Matt Nix",
        "profile_path": "/qvfbD7kc7nU3RklhFZDx9owIyrY.jpg"
      }
    ],
  "episode_run_time": [
      45
    ],
  "first_air_date": "2007-06-28",
  "genres": [
      {
        "id": 10759,
        "name": "Action & Adventure"
      },
      {
        "id": 18,
        "name": "Drama"
      }
    ],
  "homepage": "http://usanetwork.com/burnnotice",
  "id": 2919,
  "in_production": false,
  "languages": [
      "en"
    ],
  "last_air_date": "2013-09-12",
  "last_episode_to_air": {
      "air_date": "2013-09-12",
      "episode_number": 13,
      "id": 224759,
      "name": "Reckoning",
      "overview": "Series Finale. Michael must regain the trust of those closest to him in order to finish what he started.",
      "production_code": null,
      "season_number": 7,
      "show_id": 2919,
      "still_path": "/lGdhFXEi29e2HeXMzgA9bvvIIJU.jpg",
      "vote_average": 0,
      "vote_count": 0
    },
  "name": "Burn Notice",
  "next_episode_to_air": null,
  "networks": [
      {
        "name": "USA Network",
        "id": 30,
        "logo_path": "/g1e0H0Ka97IG5SyInMXdJkHGKiH.png",
        "origin_country": "US"
      }
    ],
  "number_of_episodes": 111,
  "number_of_seasons": 7,
  "origin_country": [
      "US"
    ],
  "original_language": "en",
  "original_name": "Burn Notice",
  "overview": "A formerly blacklisted spy uses his unique skills and training to help people in desperate situations.",
  "popularity": 12.174,
  "poster_path": "/A9EmzNtwfEnXYdMTgGKcL4kiZm2.jpg",
  "production_companies": [
      {
        "id": 6981,
        "logo_path": null,
        "name": "Fuse Entertainment",
        "origin_country": ""
      },
      {
        "id": 6529,
        "logo_path": null,
        "name": "Fox Television Studios",
        "origin_country": ""
      }
    ],
  "seasons": [
      {
        "_id": "525749cc19c29531db0988cc",
        "air_date": "2011-04-17",
        "episodes": [
            {
              "air_date": "2011-04-17",
              "episode_number": 1,
              "id": 224659,
              "name": "The Fall of Sam Axe",
              "overview": "The Fall of Sam Axe is a 2011 American television film based on the USA Network television series Burn Notice. It was the first official Burn Notice spin-off, and was directed by Jeffrey Donovan. The show was broadcast in the United States on April 17, 2011, on the U.S. television network USA Network. The film, while not an episode of the show, introduced plot elements for the show's fifth season.",
              "production_code": null,
              "season_number": 0,
              "show_id": 2919,
              "still_path": "/dchZWB1GoBuk1l4HFtmbVdIfE6w.jpg",
              "vote_average": 0,
              "vote_count": 0,
              "crew": [
                  {
                    "id": 52886,
                    "credit_id": "525749ee19c29531db09a624",
                    "name": "Jeffrey Donovan",
                    "department": "Directing",
                    "job": "Director",
                    "profile_path": "/5i47zZDpnAjLBtQdlqhg5AIYCuT.jpg"
                  },
                  {
                    "id": 1233032,
                    "credit_id": "525749d019c29531db098a46",
                    "name": "Matt Nix",
                    "department": "Writing",
                    "job": "Writer",
                    "profile_path": null
                  }
                ],
              "guest_stars": []
            }
          ],
        "name": "Specials",
        "overview": "",
        "poster_path": "/AoMKrA2j56SrQMBc3hp5jVmLv3B.jpg",
        "season_number": 0
      },
...
```

