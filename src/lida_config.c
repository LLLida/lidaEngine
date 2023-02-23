/*
  Configuration through INI files.
 */

enum Config_Entry_Type {
  CONFIG_INTEGER,
  CONFIG_FLOAT,
  CONFIG_STRING
};

typedef struct {

  char* name;
  union {
    int int_;
    float float_;
    char* str;
  } value;
  int type;

} Config_Entry;

DECLARE_COMPONENT(Config_Entry);


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

INTERNAL uint32_t
HashConfigEntry(const void* obj)
{
  const Config_Entry* entry = obj;
  return HashString32(entry->name);
}

INTERNAL int
CompareConfigEntries(const void* lhs, const void* rhs)
{
  const Config_Entry* l = lhs, *r = rhs;
  return strcmp(l->name, r->name);
}


/// public functions

// Parse INI file.
INTERNAL void
ParseConfig(const char* filename, Fixed_Hash_Table* vars, char* buff, size_t buff_size)
{
  size_t sz;
  char* file_contents = PlatformLoadEntireFile(filename, &sz);
  char* line = file_contents;
  int lineno = 1;
  char* current_section = NULL;
  size_t buff_offset = 0;
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
        LOG_INFO("%d section: '%s'", lineno, current_section);
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
          // allocate a string for name
          Config_Entry entry;
          entry.name = buff + buff_offset;
          buff_offset += stbsp_sprintf(entry.name, "%s.%s", current_section, name) + 1;
          if (buff_offset >= buff_size) {
            LOG_WARN("out of memory when parsing file '%s'", filename);
          }
          // parse value
          if ((value[0] >= '0' && value[0] <= '9') ||
              value[0] == '-') {
            // parse number
            if (strchr(value, '.')) {
              entry.value.float_ = strtof(value, NULL);
              entry.type = CONFIG_FLOAT;
              // LOG_INFO("%d: float: %s.%s -> %f", lineno, current_section, name, entry.value.float_);
            } else {
              entry.value.int_ = atoi(value);
              // LOG_INFO("%d: integer: %s.%s -> %d", lineno, current_section, name, num);
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
            // LOG_INFO("%d: string: %s.%s -> %s", lineno, current_section, name, value);
            entry.value.str = buff + buff_offset;
            buff_offset += strlen(value) + 1;
            if (buff_offset >= buff_size) {
              LOG_WARN("out of memory when parsing file '%s'", filename);
            }
            strcpy(entry.value.str, value);
          }
          // insert to hash table
          // TODO: check if hash table is out of space
          FHT_Insert(vars, &type_info_Config_Entry, &entry);
        }
      }
    }
    // goto next line
    line = (next) ? (next+1) : NULL;
    lineno++;
  }
  PlatformFreeFile(file_contents);
}
