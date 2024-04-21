/*
 * @Author: LiuHao
 * @Date: 2024-04-21 23:01:04
 * @Description: 
 */
#include "gtest/gtest.h"

struct AB {
    int a;
    char b;
    int c;
};

struct A {
    int a;
    char b;
};

TEST(Object, Exchange)
{
    struct AB *ab = (struct AB *)malloc(sizeof(struct AB));
    ab->a = 2;
    ab->b = 'a';
    ab->c = 3;
    struct A *a = (struct A*)ab;
    EXPECT_EQ(a->a, 2);
    EXPECT_EQ(a->b, 'a');
}