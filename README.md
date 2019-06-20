# *cap*

### More preprocessing for C

---

### What is it and how can I use *cap* ?

**cap** is a command line tool for applying additional preprocessing to a C source file before handing it to the C preprocessor.  So in use you would do something like :

```shell
cap myfile.c | cpp > out.c
```

or

```shell
cap -o out.c myfile.c
```

It has some features you will hopefully find useful in C programming, including the ability to pass sections of the input file to any other application and write that application's output to cap's output file.

Cap provides many additional directives.  Here's an example :

```C
#quote #define mymacro(a,b,c)
if( a == b  )
{
    if( a > c )
    {
        do_a() ;
    }
    else
    {
        do_c() ;
    }
}
#
```

This would be converted by **cap** into :

```C
#define mymacro(a,b,c) \
if( a == b  ) \
{ \
    if( a > c ) \
    { \
        do_a() ; \
    } \
    else \
    { \
        do_c() ; \
    } \
}
```

In this particular example \#quote adds continuation marks to the end of lines, which is important for multiline macros.


## **cap** commands

#### **\# **

A single hash on a line is commonly used to denote the end of a effect ( like *\#quote ).  If you use *\#macrochar* to set the directive line identifier as something else then use one of those characters instead of a hash.

#### **\#skipon** and **\#skipoff**

This simply tells cap to pass anything that follows directly to output without processing it.  This is useful to avoid potential clashes or simply for efficiency.

#### **\#macrochar** *&lt;some-character&gt;*

Allows you to change the character used to recognize directives, which defaults to a hash ( '\#' ).  Provided to allow flexibility.

#### **\#quote**

As you've already seen this adds continuation marks to the end of lines.  Adding those yourself can be an irritation for editing.  This lets the computer sort it out for you.  Quoting ends when the first line with just a hash on it is encountered.

#### **\#comment**

This simply outputs what follows in a well formatted C-style comment block.  Stops when a single hash on a line is encountered.  For example :

```C
#comment
This is a comment with two lines
and this is the second line.
#
```

  produces :

```C
/*
 * This is a comment with two lines
 * and this is the second line.
 */
```

#### **\#def** *&lt;macro-definition&gt; &lt;macro-code&gt;*

This helps in writing macros that take parameters and/or are multiline.  Ends when it encounters a single hash on a line.

In C macros that take paramters it is common to want to to bracket those parameters in the macro body to protect the parameters fom side effects of macro expansion.  Multiline macros require continuation marks on the end of every line.  These are tedious to do and the bracketing can create a "bracket hell" for coders.  Using *&lt;def;&lt;* lets the computer handle that tedium for you.

And example :
```C
#def mything( a, bc, de )
if( a > bc )
    de = a ;
else
    de = a|bc^ (a^bc) ;
#
```
Output :
```C
#define mything( a, bc, de ) \
if( (a) > (bc) ) \
    (de) = (a) ; \
else \
    (de) = (a)|(bc)^ ((a)^(bc)) ;
```


#### **\#constants** *&lt;prefix&gt; &lt;postfix&gt;*

Generate a set of defined constants.  An example is the simplest way to illustrate :

```C
#constants SJG TEST
FIRST
SECOND
THIRD
#
```
```C
#define SJG_FIRST_TEST		0
#define SJG_SECOND_TEST		SJG_FIRST_TEST + 1
#define SJG_THIRD_TEST		SJG_FIRST_TEST + 2
```
Notice how the constant names and values have been built.  This is tedious to type and again the computer can do it for us with *cap*.

#### **\#flags**

Similar to *\#constants* but generating values suitable for use as flags ( powers of two ! ).

```C
#flags SJG TEST
ALPHA
BETA
GAMMA
#
```
```C
#define SJG_ALPHA_FLAG		0x01
#define SJG_BETA_FLAG		0x02
#define SJG_GAMMA_FLAG		0x04
```

#### **\#constants-values**

Again like *\#constants* but This time writing explicit incrementing values rather than values relatve to the preceeding definition.  An example :

```C
#constants-values SJG VALS
FIRST
SECOND

THIRD
#
```
```C
#define SJG_FIRST_VALS		0
#define SJG_SECOND_VALS		1
#define SJG_THIRD_VALS		2
```

#### **\#constants-negative**

This is like *\=constants-values* but producing negative values ( 0, -1, -2, -3 ... ) instead of positive ones.

#### **\#command**

Send a all input from after the directive to the next single hash on a line as input ( on stdin ) to another external command.  The external command **must** output to stdout.

If we had a command called *toupper* that converted input to uppercase then we would get this :

```C
#command toupper
this was all in lower case
and should be in upper case
in the output
#
```
```C
THIS WAS ALL IN LOWER CASE
AND SHOULD BE IN UPPER CASE
IN THE OUTPUT
```

This example is trivial, but you could use the *\#command* directive to generate code using Python or a C application or anything like that.

#### **\#redefine**

C's preprocessor will throw a fit if you try to define a macro that already exists.  THis simply ensures that the macro is undefined first.  It can be used with single or multiline macros.

#### **\#brace_macros_on**

Braces ( '{' and '}' ) in C denote scope levels and it is sometimes desirable to have functions called on entry toa new scope and on exit.  Unfortunately there is no standard way to do this in C ( just what do the C standard's people do all day ? ).  This is something to help with that without cluttering up your code.

This command turns on that substitution.  It will continue until it is turned off.  Note that braces can be included in the macro you expand to and they will *not* be processed ( no nasty recursion to crash things ).

Note that you **replace** the brace using this, so if you need a brace in the code then you **must** explicitly include it in the macro definition.  This allows more flexibility.  Note how the macros are defined in the example.

Here's an example :
```C
#def_open_brace { hello() ;

#def_close_brace goodbye() ; }

#brace_macros_on

if( a )
{
    doit() ;
}
```
generates :
```C
if( a )
{ hello() ;
    doit() ;
goodbye() ; }
```

Note also the related *return_macro* functionality.  As you would expect only "naked" braces will be replaced.  Braces inside quotes or comments will **not** be replaced.

Possible uses include debugging and placing hook calls to control e.g. custom memory allocation and freeing when you exit a scope.

#### **\#brace_macros_off**

Simply stops replacing brace macros.

#### **\#def_open_brace**

Everything to end of line after the directive is substutued for '{' when brace substitution in on.

#### **\#def_close_brace**

Everything to end of line after the directive is substutued for '}' when brace substitution in on.

#### **\#def_return_macro**

Like the brace closing macro but this is triggered by any return statement.  The effect is like this :

```C
#def_return_macro my_pre_return_fn() ;

#return_macro_on

if( something )
{
    dothis() ;

    return x ;
}

#return_macro_off
```
```C
if( something )
{
    dothis() ;
    
    { my_pre_return_fn() ; return x ; }
}
```

Normally you would turn on both brace and return macros at the same time, but you need to make that choice explicitly.

Three forms of return are captured :
```C
return ;
return( something ) ;
return something ;
```

#### **\#return_macro_on**

Turns the return macro functionality one.

#### **\#return_macro_**

Turns the return macro functionality off.


