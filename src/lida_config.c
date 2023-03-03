/*
  Configuration through INI files.
 */

enum CVar_Type {
  CONFIG_INTEGER,
  CONFIG_FLOAT,
  CONFIG_STRING
};

typedef struct {

  union {
    int int_;
    float float_;
    char* str;
  } value;
  int type;

} CVar;

DECLARE_TYPE(CVar);

typedef struct Ternary_Tree_Node Ternary_Tree_Node;

struct Ternary_Tree_Node {

  Ternary_Tree_Node* left;
  Ternary_Tree_Node* mid;
  Ternary_Tree_Node* right;
  int is_end;
  char splitchar;

};

typedef void(*Traverse_String_Func)(char* str);

typedef struct {

  Ternary_Tree_Node* root;
  char buff[8192];
  uint32_t buff_offset;

} Config_File;

DECLARE_COMPONENT(Config_File);

GLOBAL Config_File* g_config;


/// private functions

INTERNAL int
IsSpace(char c)
{
  /*
    ‘ ‘ – Space
    ‘\t’ – Horizontal tab
    ‘\n’ – Newline
    ‘\v’ – Vertical tab
    ‘\f’ – Feed
    ‘\r’ – Carriage return
   */
  switch (c)
    {
    case ' ':
    case '\t':
    case '\n':
    case '\v':
    case '\f':
    case '\r':
      return 1;
    }
  return 0;
}

INTERNAL char*
StripSpacesRight(char* s)
{
  char* p = s + strlen(s);
  while (p > s && IsSpace(*--p))
    *p = '\0';
  return s;
}

/* Return pointer to first non-whitespace char in given string. */
INTERNAL char*
SkipSpacesLeft(char* s)
{
  while (*s && IsSpace(*s))
    s++;
  return (char*)s;
}

INTERNAL Ternary_Tree_Node*
TST_New(Config_File* config, char c)
{
  if (config->buff_offset >= sizeof(config->buff)) {
    LOG_WARN("out of memory when parsing INI file");
    config->buff_offset = 0;
  }
  Ternary_Tree_Node* node = (void*)&config->buff[config->buff_offset];
  config->buff_offset += sizeof(Ternary_Tree_Node);
  node->splitchar = c;
  node->left = NULL;
  node->right = NULL;
  node->mid = NULL;
  node->is_end = 0;
  return node;
}

// return: pointer to value
INTERNAL void*
TST_Insert(Config_File* config, Ternary_Tree_Node** root, const char* word)
{
  // we don't need this to be fast, using recursion is just fine
  if (!(*root))
    *root = TST_New(config, *word);
  if ((*word) < (*root)->splitchar) {
    return TST_Insert(config, &((*root)->left), word);
  } else if ((*word) > (*root)->splitchar) {
    return TST_Insert(config, &((*root)->right), word);
  } else {
    if (*(word + 1)) {
      return TST_Insert(config, &((*root)->mid), word + 1);
    }
    (*root)->is_end = 1;
    config->buff_offset += sizeof(Ternary_Tree_Node);
    return (*root) + 1;
  }
}

INTERNAL void*
TST_Search(Config_File* config, Ternary_Tree_Node* root, const char* word)
{
  if (!root)
    return NULL;
  if (*word < root->splitchar)
    return TST_Search(config, root->left, word);
  else if (*word > root->splitchar)
    return TST_Search(config, root->right, word);
  else {
    if (*(word + 1) == '\0') {
      if (root->is_end) {
        return root + 1;
      }
      return NULL;
    }
    return TST_Search(config, root->mid, word + 1);
  }
}

INTERNAL size_t
TST_Traverse(Ternary_Tree_Node* root, Traverse_String_Func func,
             char* buff, int depth)
{
  if (root == NULL)
    return 0;
  size_t ret = 0;

  ret += TST_Traverse(root->left, func, buff, depth);

  buff[depth] = root->splitchar;
  if (root->is_end) {
    ret++;
    buff[depth + 1] = '\0';
    ret++;
    func(buff);
  }

  ret += TST_Traverse(root->mid, func, buff, depth + 1);
  ret += TST_Traverse(root->right, func, buff, depth);

  return ret;
}


/// public functions

// Parse INI file.
INTERNAL void
ParseConfig(const char* filename, Config_File* config)
{
  size_t sz;
  char* file_contents = PlatformLoadEntireFile(filename, &sz);
  char* line = file_contents;
  int lineno = 1;
  char* current_section = NULL;
  config->buff_offset = 0;
  size_t buff_size = sizeof(config->buff);
  // https://stackoverflow.com/questions/17983005/c-how-to-read-a-string-line-by-line
  while (line) {
    char* next = strchr(line, '\n');
    if (next) {
      *next = '\0';
    }
    line = SkipSpacesLeft(StripSpacesRight(line));
    if (line[0] == '#') {
      // comment, do nothing
    } else if (line[0] == '[') {
      // section
      char* s = line+1;
      current_section = s;
      s = strchr(s, ']');

      if (s == NULL) {
        LOG_ERROR("error at line %d in file '%s': no ']' was found", lineno, filename);
      } else if (s[1] != '\0') {
        LOG_ERROR("error at line %d in file '%s': incorrect section", lineno, filename);
      } else {
        // success
        s[0] = '\0';
      }

    } else if (line[0]) {
      // value
      char* eq = strchr(line, '=');
      if (eq == NULL) {
        LOG_ERROR("error at line %d in file '%s': no '=' found in value assignment",
                  lineno, filename);
      } else {
        eq[0] = '\0';
        char* name = StripSpacesRight(line);
        char* value = SkipSpacesLeft(eq+1);
        value = StripSpacesRight(value);
        if (value[0] == '\0') {
          LOG_ERROR("error at line %d in file '%s': assignment to nil",
                    lineno, filename);
        } else if (current_section == NULL) {
          LOG_ERROR("error at line %d in file '%s': variable must be below [Section] statement",
                    lineno, filename);
        } else {
          CVar entry;
          char name_full[64];
          stbsp_snprintf(name_full, sizeof(name_full), "%s.%s", current_section, name);
          // parse value
          if ((value[0] >= '0' && value[0] <= '9') ||
              value[0] == '-') {
            // parse number
            if (strchr(value, '.')) {
              entry.value.float_ = strtof(value, NULL);
              entry.type = CONFIG_FLOAT;
            } else {
              entry.value.int_ = atoi(value);
              entry.type = CONFIG_INTEGER;
            }
          } else {
            if (value[0] == '"') {
              value++;
              char* end = strchr(value, '"');
              if (end == NULL) {
                LOG_ERROR("error at line %d in file '%s': no matching \" found",
                          lineno, filename);
              } else {
                end[0] = '\0';
              }
            }
            entry.type = CONFIG_STRING;
            entry.value.str = config->buff + config->buff_offset;
            config->buff_offset += strlen(value) + 1;
            if (config->buff_offset >= buff_size) {
              LOG_WARN("out of memory when parsing file '%s'", filename);
              config->buff_offset = 0;
            }
            strcpy(entry.value.str, value);
          }
          CVar* var = TST_Insert(config, &config->root, name_full);
          memcpy(var, &entry, sizeof(CVar));
        }
      }
    }
    // goto next line
    line = (next) ? (next+1) : NULL;
    lineno++;
  }
  PlatformFreeLoadedFile(file_contents);
}

INTERNAL void
ConfigFile_ReloadFunc(void* component, const char* path, void* udata)
{
  (void)udata;
  Config_File* config = component;
  // FHT_Clear(&config->vars, GET_TYPE_INFO(CVar));
  ParseConfig(path, config);
}

INTERNAL Config_File*
CreateConfig(ECS* ecs, Asset_Manager* am, EID entity, const char* name)
{
  Config_File* config = AddComponent(ecs, Config_File, entity);
  // const int max_vars = 32;
  // FHT_Init(&config->vars,
  //          config->buff+sizeof(config->buff) - FHT_CALC_SIZE(GET_TYPE_INFO(CVar), max_vars),
  //          max_vars, GET_TYPE_INFO(CVar));
  ParseConfig(name, config);
  AddAsset(am, entity, name, &g_sparse_set_Config_File,
           ConfigFile_ReloadFunc, NULL);
  return config;
}

INTERNAL int*
GetVar_Int(Config_File* config, const char* var)
{
  // CVar* entry = FHT_Search(&config->vars, GET_TYPE_INFO(CVar), &var);
  CVar* entry = TST_Search(config, config->root, var);
  if (entry) {
    if (entry->type == CONFIG_INTEGER) {
      return &entry->value.int_;
    }
    LOG_WARN("typecheck failed");
  }
  return NULL;
}

INTERNAL float*
GetVar_Float(Config_File* config, const char* var)
{
  // CVar* entry = FHT_Search(&config->vars, GET_TYPE_INFO(CVar), &var);
  CVar* entry = TST_Search(config, config->root, var);
  if (entry) {
    if (entry->type == CONFIG_FLOAT) {
      return &entry->value.float_;
    }
    LOG_WARN("typecheck failed");
  }
  return NULL;
}

INTERNAL const char*
GetVar_String(Config_File* config, const char* var)
{
  // CVar* entry = FHT_Search(&config->vars, GET_TYPE_INFO(CVar), &var);
  CVar* entry = TST_Search(config, config->root, var);
  if (entry) {
    if (entry->type == CONFIG_STRING) {
      return entry->value.str;
    }
    LOG_WARN("typecheck failed");
  }
  return NULL;
}

INTERNAL size_t
ListVars(Config_File* config, Traverse_String_Func func)
{
  char buff[256];
  return TST_Traverse(config->root, func, buff, 0);
}

INTERNAL size_t
ListVarsPrefix(Config_File* config, Traverse_String_Func func, const char* prefix)
{
  // TODO: implement
  return 0;
}
