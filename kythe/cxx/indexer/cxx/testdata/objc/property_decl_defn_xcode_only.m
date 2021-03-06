// This test works with the default version of clang on darwin and does not
// work with our minimum version of clang on linux.
//
// Checks that Objective-C properties are declared and defined. This test uses
// syntax that isn't supported by default in clang, however it is syntax that
// is supported with newer versions of xcode.

//- @Box defines/binding BoxDecl
//- BoxDecl.node/kind record
//- BoxDecl.subkind class
//- BoxDecl.complete incomplete
@interface Box {
  //- @width defines/binding WidthIVarDecl
  int width;
}

//- @a defines/binding ADecl
@property int a;

//- @b defines/binding BDecl
//- BDecl.node/kind variable
//- BDecl.subkind field
//- @b defines/binding BIvarDecl
//- BIvarDecl.node/kind variable
//- BIvarDecl.subkind field
@property int b;

//- @c defines/binding CDecl
@property int c;

//- @d defines/binding DDecl
@property int d;

-(void) foo;
@end


//- @Box defines/binding BoxImpl
//- BoxImpl.node/kind record
//- BoxImpl.subkind class
//- BoxImpl.complete definition
@implementation Box

@synthesize a = width;

//- @c defines/binding CIVarDecl
@synthesize c;

//- @newvar defines/binding NewvarIVarDecl
@synthesize d = newvar;

-(void) foo {
  //- @width ref WidthIVarDecl
  self->width = 100;

  //- @a ref ADecl
  self.a = 200;

  //- @b ref BDecl
  self.b = 300;
  //- @"_b" ref BIvarDecl
  self->_b = 301;

  //- @c ref CDecl
  self.c = 400;
  //- @c ref CIvarDecl
  self->c = 401;

  //- @d ref DDecl
  self.d = 500;
  //- @newvar ref NewvarIVarDecl
  self->newvar = 501;
}
@end

int main(int argc, char **argv) {
  return 0;
}

