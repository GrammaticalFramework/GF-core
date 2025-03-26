----------------------------------------------------------------------
-- |
-- Module      : PGFtoHaskell
-- Maintainer  : Aarne Ranta
-- Stability   : (stable)
-- Portability : (portable)
--
-- > CVS $Date: 2005/06/17 12:39:07 $
-- > CVS $Author: bringert $
-- > CVS $Revision: 1.8 $
--
-- to write a GF abstract grammar into a Haskell module with translations from
-- data objects into GF trees. Example: GSyntax for Agda.
-- AR 11/11/1999 -- 7/12/2000 -- 18/5/2004
-----------------------------------------------------------------------------

module GF.Compile.PGFtoHaskell (grammar2haskell) where

import PGF2

import GF.Data.Operations
import GF.Infra.Option

import Data.List(isPrefixOf,find,intercalate,intersperse,groupBy,sortBy)
import Data.Maybe(mapMaybe)
import qualified Data.Map as Map

type Prefix = String -> String
type DerivingClause = String

-- | the main function
grammar2haskell :: Options
                -> String  -- ^ Module name.
                -> PGF
                -> String
grammar2haskell opts name gr = foldr (++++) [] $
  pragmas ++ haskPreamble gadt name derivingClause (extraImports ++ pgfImports) ++
  [types, gfinstances gId lexical gr'] ++ compos
    where gr' = hSkeleton gr
          gadt = haskellOption opts HaskellGADT
          dataExt = haskellOption opts HaskellData
          lexical cat = haskellOption opts HaskellLexical && isLexicalCat opts cat
          gId | haskellOption opts HaskellNoPrefix = rmForbiddenChars
              | otherwise = ("G"++) . rmForbiddenChars
          -- GF grammars allow weird identifier names inside '', e.g. 'VP/Object'
          rmForbiddenChars = filter (`notElem` "'!#$%&*+./<=>?@\\^|-~")
          pragmas | gadt = ["{-# LANGUAGE GADTs, FlexibleInstances, KindSignatures, RankNTypes, TypeSynonymInstances #-}"]
                  | dataExt = ["{-# LANGUAGE DeriveDataTypeable #-}"]
                  | otherwise = []
          derivingClause
                 | dataExt = "deriving (Show,Data)"
                 | otherwise = "deriving Show"
          extraImports | gadt = ["import Control.Monad.Identity", "import Data.Monoid"]
                       | dataExt = ["import Data.Data"]
                       | otherwise = []
          pgfImports = ["import PGF2", ""]
          types | gadt = datatypesGADT gId lexical gr'
                | otherwise = datatypes gId derivingClause lexical gr'
          compos | gadt = prCompos gId lexical gr' ++ composClass
                 | otherwise = []

haskPreamble :: Bool -> String -> String -> [String] -> [String]
haskPreamble gadt name derivingClause imports =
 [
  "module " ++ name ++ " where",
  ""
 ] ++ imports ++ [
  "",
  "----------------------------------------------------",
  "-- automatic translation from GF to Haskell",
  "----------------------------------------------------",
  "",
  "class Gf a where",
  "  gf :: a -> Expr",
  "  fg :: Expr -> a",
  "",
  predefInst gadt derivingClause "GString" "String"  "unStr"    "mkStr",
  "",
  predefInst gadt derivingClause "GInt"    "Integer"     "unInt"    "mkInt",
  "",
  predefInst gadt derivingClause "GFloat"  "Double"  "unFloat"  "mkFloat",
  "",
  "----------------------------------------------------",
  "-- below this line machine-generated",
  "----------------------------------------------------",
  ""
 ]

predefInst :: Bool -> String -> String -> String -> String -> String -> String
predefInst gadt derivingClause gtyp typ destr consr =
  (if gadt
    then []
    else "newtype" +++ gtyp +++ "=" +++ gtyp +++ typ +++ derivingClause ++ "\n\n"
    )
  ++
  "instance Gf" +++ gtyp +++ "where" ++++
  "  gf (" ++ gtyp +++ "x) =" +++ consr +++ "x" ++++
  "  fg t =" ++++
  "    case "++destr++" t of" ++++
  "      Just x  -> " +++ gtyp +++ "x" ++++
  "      Nothing -> error (\"no" +++ gtyp +++ "\" ++ show t)"

type OIdent = String

type HSkeleton = [(OIdent, [(OIdent, [OIdent])])]

datatypes :: Prefix -> DerivingClause -> (OIdent -> Bool) -> (String,HSkeleton) -> String
datatypes gId derivingClause lexical = foldr (+++++) "" . filter (/="") . map (hDatatype gId derivingClause lexical) . snd

gfinstances :: Prefix -> (OIdent -> Bool) -> (String,HSkeleton) -> String
gfinstances gId lexical (m,g) = foldr (+++++) "" $ filter (/="") $ map (gfInstance gId lexical m) g


hDatatype  :: Prefix -> DerivingClause -> (OIdent -> Bool) -> (OIdent, [(OIdent, [OIdent])]) -> String
hDatatype _ _ _ ("Cn",_) = "" ---
hDatatype gId _ _ (cat,[]) = "data" +++ gId cat
hDatatype gId derivingClause _ (cat,rules) | isListCat (cat,rules) =
 "newtype" +++ gId cat +++ "=" +++ gId cat +++ "[" ++ gId (elemCat cat) ++ "]"
  +++ derivingClause
hDatatype gId derivingClause lexical (cat,rules) =
 "data" +++ gId cat +++ "=" ++
 (if length rules == 1 then "" else "\n  ") +++
 foldr1 (\x y -> x ++ "\n |" +++ y) constructors ++++
 " " +++ derivingClause
  where
    constructors = [gId f +++ foldr (+++) "" (map (gId) xx) | (f,xx) <- nonLexicalRules (lexical cat) rules]
                   ++ if lexical cat then [lexicalConstructor cat +++ "String"] else []

nonLexicalRules :: Bool -> [(OIdent, [OIdent])] -> [(OIdent, [OIdent])]
nonLexicalRules False rules = rules
nonLexicalRules True rules = [r | r@(f,t) <- rules, not (null t)]

lexicalConstructor :: OIdent -> String
lexicalConstructor cat = "Lex" ++ cat

predefTypeSkel :: HSkeleton
predefTypeSkel = [(c,[]) | c <- ["String", "Int", "Float"]]

-- GADT version of data types
datatypesGADT :: Prefix -> (OIdent -> Bool) -> (String,HSkeleton) -> String
datatypesGADT gId lexical (_,skel) = unlines $
    concatMap (hCatTypeGADT gId) (skel ++ predefTypeSkel) ++
    [
      "",
      "data Tree :: * -> * where"
    ] ++
    concatMap (map ("  "++) . hDatatypeGADT gId lexical) skel ++
    [
      "  GString :: String -> Tree GString_",
      "  GInt :: Int -> Tree GInt_",
      "  GFloat :: Double -> Tree GFloat_",
      "",
      "instance Eq (Tree a) where",
      "  i == j = case (i,j) of"
    ] ++
    concatMap (map ("    "++) . hEqGADT gId lexical) skel ++
    [
      "    (GString x, GString y) -> x == y",
      "    (GInt x, GInt y) -> x == y",
      "    (GFloat x, GFloat y) -> x == y",
      "    _ -> False"
    ]

hCatTypeGADT :: Prefix -> (OIdent, [(OIdent, [OIdent])]) -> [String]
hCatTypeGADT gId (cat,rules)
    = ["type"+++gId cat+++"="+++"Tree"+++gId cat++"_",
       "data"+++gId cat++"_"]

hDatatypeGADT :: Prefix -> (OIdent -> Bool) -> (OIdent, [(OIdent, [OIdent])]) -> [String]
hDatatypeGADT gId lexical (cat, rules)
    | isListCat (cat,rules) = [gId cat+++"::"+++"["++gId (elemCat cat)++"]" +++ "->" +++ t]
    | otherwise =
        [ gId f +++ "::" +++ concatMap (\a -> gId a +++ "-> ") args ++ t
          | (f,args) <- nonLexicalRules (lexical cat) rules ]
        ++ if lexical cat then [lexicalConstructor cat +++ ":: String ->"+++ t] else []
  where t = "Tree" +++ gId cat ++ "_"

hEqGADT :: Prefix -> (OIdent -> Bool) -> (OIdent, [(OIdent, [OIdent])]) -> [String]
hEqGADT gId lexical (cat, rules)
  | isListCat (cat,rules) = let r = listr cat in ["(" ++ patt "x" r ++ "," ++ patt "y" r ++ ") -> " ++ listeqs]
  | otherwise = ["(" ++ patt "x" r ++ "," ++ patt "y" r ++ ") -> " ++ eqs r | r <- nonLexicalRules (lexical cat) rules]
          ++ if lexical cat then ["(" ++ lexicalConstructor cat +++ "x" ++ "," ++ lexicalConstructor cat +++ "y" ++ ") -> x == y"] else []

 where
   patt s (f,xs) = unwords (gId f : mkSVars s (length xs))
   eqs (_,xs) = unwords ("and" : "[" : intersperse "," [x ++ " == " ++ y |
     (x,y) <- zip (mkSVars "x" (length xs)) (mkSVars "y" (length xs)) ] ++ ["]"])
   listr c = (c,["foo"]) -- foo just for length = 1
   listeqs = "and [x == y | (x,y) <- zip x1 y1]"

prCompos :: Prefix -> (OIdent -> Bool) -> (String,HSkeleton) -> [String]
prCompos gId lexical (_,catrules) =
    ["instance Compos Tree where",
     "  compos r a f t = case t of"]
    ++
    ["    " ++ prComposCons (gId f) xs | (c,rs) <- catrules, not (isListCat (c,rs)),
                                         (f,xs) <- rs, not (null xs)]
    ++
    ["    " ++ prComposCons (gId c) ["x1"] | (c,rs) <- catrules, isListCat (c,rs)]
    ++
    ["    _ -> r t"]
  where
    prComposCons f xs = let vs = mkVars (length xs) in
                        f +++ unwords vs +++ "->" +++ rhs f (zip vs xs)
    rhs f vcs = "r" +++ f +++ unwords (map (prRec f) vcs)
    prRec f (v,c)
      | isList f  = "`a` foldr (a . a (r (:)) . f) (r [])" +++ v
      | otherwise = "`a`" +++ "f" +++ v
    isList f = gId "List" `isPrefixOf` f

gfInstance :: Prefix -> (OIdent -> Bool) -> String -> (OIdent, [(OIdent, [OIdent])]) -> String
gfInstance gId lexical m crs = hInstance gId lexical m crs ++++ fInstance gId lexical m crs

hInstance :: (String -> String) -> (String -> Bool) -> String -> (String, [(OIdent, [OIdent])]) -> String
----hInstance m ("Cn",_) = "" --- seems to belong to an old applic. AR 18/5/2004
hInstance gId _ m (cat,[]) = unlines [
  "instance Show" +++ gId cat,
  "",
  "instance Gf" +++ gId cat +++ "where",
  "  gf _ = undefined",
  "  fg _ = undefined"
  ]
hInstance gId lexical m (cat,rules)
 | isListCat (cat,rules) =
  "instance Gf" +++ gId cat +++ "where" ++++
     "  gf (" ++ gId cat +++ "[" ++ intercalate "," baseVars ++ "])"
           +++ "=" +++ mkRHS ("Base"++ec) baseVars ++++
     "  gf (" ++ gId cat +++ "(x:xs)) = "
           ++ mkRHS ("Cons"++ec) ["x",prParenth (gId cat+++"xs")]
-- no show for GADTs
--     ++++ " gf (" ++ gId cat +++ "xs) = error (\"Bad " ++ cat ++ " value: \" ++ show xs)"
 | otherwise =
  "instance Gf" +++ gId cat +++ "where\n" ++
  unlines ([mkInst f xx | (f,xx) <- nonLexicalRules (lexical cat) rules]
            ++ if lexical cat then ["  gf (" ++ lexicalConstructor cat +++ "x) = mkApp x []"] else [])
 where
   ec = elemCat cat
   baseVars = mkVars (baseSize (cat,rules))
   mkInst f xx = let xx' = mkVars (length xx) in "  gf " ++
     (if null xx then gId f else prParenth (gId f +++ foldr1 (+++) xx')) +++
     "=" +++ mkRHS f xx'
   mkRHS f vars = "mkApp \"" ++ f ++ "\"" +++
       "[" ++ prTList ", " ["gf" +++ x | x <- vars] ++ "]"

mkVars :: Int -> [String]
mkVars = mkSVars "x"

mkSVars :: String -> Int -> [String]
mkSVars s n = [s ++ show i | i <- [1..n]]

----fInstance m ("Cn",_) = "" ---
fInstance _ _ m (cat,[]) = ""
fInstance gId lexical m (cat,rules) =
  "  fg t =" ++++
  (if isList
    then "    " ++ gId cat ++ " (fgs t) where\n     fgs t = case unApp t of"
    else "    case unApp t of") ++++
  unlines [mkInst f xx | (f,xx) <- nonLexicalRules (lexical cat) rules] ++++
  (if lexical cat then "      Just (i,[]) -> " ++ lexicalConstructor cat +++ "i" else "") ++++
  "      _ -> error (\"no" +++ cat ++ " \" ++ show t)"
   where
    isList = isListCat (cat,rules)
    mkInst f xx =
     "      Just (i," ++
     "[" ++ prTList "," xx' ++ "])" +++
     "| i == \"" ++ f ++ "\" ->" +++ mkRHS f xx'
       where
         xx' = ["x" ++ show i | (_,i) <- zip xx [1..]]
         mkRHS f vars
           | isList =
               if "Base" `isPrefixOf` f
                             then "[" ++ prTList ", " [ "fg" +++ x | x <- vars ] ++ "]"
                 else "fg" +++ (vars !! 0) +++ ":" +++ "fgs" +++ (vars !! 1)
           | otherwise =
               gId f +++
               prTList " " [prParenth ("fg" +++ x) | x <- vars]

--type HSkeleton = [(OIdent, [(OIdent, [OIdent])])]
hSkeleton :: PGF -> (String,HSkeleton)
hSkeleton gr = 
  (abstractName gr,
   let fs = 
         [(c, [(f, cs) | (f, cs,_) <- fs]) | 
                                        fs@((_, _,c):_) <- fns]
   in fs ++ [(c, []) | c <- cts, notElem c (["Int", "Float", "String"] ++ map fst fs)]
  )
 where
   cts = categories gr
   fns = groupBy valtypg (sortBy valtyps (mapMaybe jty (functions gr)))
   valtyps (_,_,x) (_,_,y) = compare x y
   valtypg (_,_,x) (_,_,y) = x == y
   jty f = case functionType gr f of
             Just ty -> let (hypos,valcat,_) = unType ty
                        in Just (f,[argcat | (_,_,ty) <- hypos, let (_,argcat,_) = unType ty],valcat)
             Nothing -> Nothing
{-
updateSkeleton :: OIdent -> HSkeleton -> (OIdent, [OIdent]) -> HSkeleton
updateSkeleton cat skel rule =
 case skel of
   (cat0,rules):rr | cat0 == cat -> (cat0, rule:rules) : rr
   (cat0,rules):rr               -> (cat0, rules) : updateSkeleton cat rr rule
-}
isListCat :: (OIdent, [(OIdent, [OIdent])]) -> Bool
isListCat (cat,rules) = "List" `isPrefixOf` cat && length rules == 2
        && ("Base"++c) `elem` fs && ("Cons"++c) `elem` fs
    where
      c = elemCat cat
      fs = map fst rules

-- | Gets the element category of a list category.
elemCat :: OIdent -> OIdent
elemCat = drop 4
{-
isBaseFun :: OIdent -> Bool
isBaseFun f = "Base" `isPrefixOf` f

isConsFun :: OIdent -> Bool
isConsFun f = "Cons" `isPrefixOf` f
-}
baseSize :: (OIdent, [(OIdent, [OIdent])]) -> Int
baseSize (_,rules) = length bs
    where Just (_,bs) = find (("Base" `isPrefixOf`) . fst) rules

composClass :: [String]
composClass =
    [
     "",
     "class Compos t where",
     "  compos :: (forall a. a -> m a) -> (forall a b. m (a -> b) -> m a -> m b)",
     "         -> (forall a. t a -> m (t a)) -> t c -> m (t c)",
     "",
     "composOp :: Compos t => (forall a. t a -> t a) -> t c -> t c",
     "composOp f = runIdentity . composOpM (Identity . f)",
     "",
     "composOpM :: (Compos t, Monad m) => (forall a. t a -> m (t a)) -> t c -> m (t c)",
     "composOpM = compos return ap",
     "",
     "composOpM_ :: (Compos t, Monad m) => (forall a. t a -> m ()) -> t c -> m ()",
     "composOpM_ = composOpFold (return ()) (>>)",
     "",
     "composOpMonoid :: (Compos t, Monoid m) => (forall a. t a -> m) -> t c -> m",
     "composOpMonoid = composOpFold mempty mappend",
     "",
     "composOpMPlus :: (Compos t, MonadPlus m) => (forall a. t a -> m b) -> t c -> m b",
     "composOpMPlus = composOpFold mzero mplus",
     "",
     "composOpFold :: Compos t => b -> (b -> b -> b) -> (forall a. t a -> b) -> t c -> b",
     "composOpFold z c f = unC . compos (\\_ -> C z) (\\(C x) (C y) -> C (c x y)) (C . f)",
     "",
     "newtype C b a = C { unC :: b }"
    ]
