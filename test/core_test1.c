/*
  This tests core engine utilities. This maybe the most important test.
 */
#include "base.h"

#include <string.h>
#include <assert.h>

static void test1();
static void test2();
static void test3();

int
main(int argc, char** argv)
{
  lida_InitPlatformSpecificLoggers();
  /*
    1. Test logging.
   */
  test1();
  LIDA_LOG_TRACE("-------------Test 1 passed--------------");

  /*
    2. Test hash tables.
   */
  test2();
  LIDA_LOG_TRACE("-------------Test 2 passed--------------");

  /*
    3. Test dynamic arrays.
   */
  test3();
  LIDA_LOG_TRACE("-------------Test 3 passed--------------");
  return 0;
}

void test1() {
  LIDA_LOG_TRACE("This is a TRACE message");
  LIDA_LOG_DEBUG("This is a DEBUG message");
  LIDA_LOG_INFO("This is a INFO message");
  LIDA_LOG_WARN("This is a WARN message");
  LIDA_LOG_ERROR("This is a ERROR message");
  LIDA_LOG_FATAL("This is a FATAL message");
}

typedef struct {
  const char* name;
  int age;
} Person;

uint32_t hash_person(const void* data) {
  const Person* person = (Person*)data;
  return lida_HashString(person->name);
}

int cmp_person(const void* lhs, const void* rhs) {
  const Person* left = lhs, *right = rhs;
  return strcmp(left->name, right->name);
}

void test2() {
  lida_TypeInfo desc = LIDA_TYPE_INFO(Person, lida_MallocAllocator(), hash_person, cmp_person, 0);
  lida_HashTable ht = LIDA_HT_EMPTY(&desc);

  Person singers[] = {
    { "Avril", 40 },
    { "Rihanna", 32 },
    { "Magnus", 32 },
    { "Levy", 28 }
  };
  /* lida_HT_Reserve(&ht, 5); */
  // insert all elements
  for (uint32_t i = 0; i < sizeof(singers) / sizeof(Person); i++) {
    lida_HT_Insert(&ht, &singers[i]);
  }
  // check if hash table finds them correctly
  for (uint32_t i = 0; i < sizeof(singers) / sizeof(Person); i++) {
    Person* singer = lida_HT_Search(&ht, &singers[i]);
    assert(singer->name == singers[i].name && singer->age == singers[i].age);
  }

  lida_HT_Iterator it;

  LIDA_HT_FOREACH(&ht, &it) {
    Person* person = lida_HT_Iterator_Get(&it);
    LIDA_LOG_TRACE("{%s, %d}", person->name, person->age);
  }

  lida_HT_Delete(&ht);
}

void test3() {
  lida_TypeInfo desc = LIDA_TYPE_INFO(Person, lida_MallocAllocator(), NULL, NULL, 0);
  lida_DynArray array = LIDA_DA_EMPTY(&desc);

  Person chads[] = {
    { "Euler", 2718281828 },
    { "Taylor", 10 },
    { "Gromov", 1917 },
    { "Bratus", 83 },
    { "Gaga", 30 }
  };

  assert(lida_DynArrayGet(&array, 0) == NULL);

  LIDA_DA_PUSH_BACK(&array, Person, .name=chads[0].name, .age=chads[0].age);
  Person* tmp = lida_DynArrayGet(&array, 0);
  assert(tmp->name == chads[0].name && tmp->age == chads[0].age);

  LIDA_DA_PUSH_BACK(&array, Person, .name=chads[1].name, .age=chads[1].age);

  LIDA_DA_PUSH_BACK(&array, Person, .name=chads[2].name, .age=chads[2].age);

  LIDA_DA_PUSH_BACK(&array, Person, .name=chads[3].name, .age=chads[3].age);

  LIDA_DA_INSERT(&array, 1, Person, .name=chads[4].name, .age=chads[4].age);
  tmp = lida_DynArrayGet(&array, 1);
  assert(tmp->name == chads[4].name && tmp->age == chads[4].age);

  assert(LIDA_DA_SIZE(&array) == 5);

  lida_DynArrayDelete(&array);
}
