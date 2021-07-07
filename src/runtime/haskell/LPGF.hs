{-# LANGUAGE LambdaCase #-}
{-# LANGUAGE OverloadedStrings #-}
{-# LANGUAGE ScopedTypeVariables #-}

-- | Linearisation-only grammar format.
-- Closely follows description in Section 2 of Angelov, Bringert, Ranta (2009):
-- "PGF: A Portable Run-Time Format for Type-Theoretical Grammars".
-- http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.640.6330&rep=rep1&type=pdf
module LPGF (
  -- ** Types
  LPGF (..), Abstract (..), Concrete (..), LinFun (..),

  -- ** Reading/writing
  readLPGF, LPGF.encodeFile,

  -- ** Linearization
  linearize, linearizeText, linearizeConcrete, linearizeConcreteText,

  -- ** Other
  abstractName,
  PGF.showLanguage, PGF.readExpr,

  -- ** DEBUG only, to be removed
  render, pp
) where

import PGF (Language)
import PGF.CId
import PGF.Expr (Expr, Literal (..))
import PGF.Tree (Tree (..), expr2tree, prTree)
import qualified PGF

-- import qualified Control.Exception as EX
import Control.Monad (liftM, liftM2, forM_)
import qualified Control.Monad.Writer as CMW
import Data.Binary (Binary, put, get, putWord8, getWord8, encodeFile, decodeFile)
import Data.Either (isLeft)
import qualified Data.IntMap as IntMap
import qualified Data.Map.Strict as Map
import Data.Text (Text)
import qualified Data.Text as T
import qualified Data.Text.Encoding as TE
import Numeric (showFFloat)
import Text.Printf (printf)

import Prelude hiding ((!!))
import qualified Prelude

-- | Linearisation-only PGF
data LPGF = LPGF {
  absname   :: CId,
  abstract  :: Abstract,
  concretes :: Map.Map CId Concrete
} deriving (Show)

-- | Abstract syntax (currently empty)
data Abstract = Abstract {
} deriving (Show)

-- | Concrete syntax
data Concrete = Concrete {
  toks    :: IntMap.IntMap Text, -- ^ all strings are stored exactly once here
  -- lincats :: Map.Map CId LinType, -- ^ a linearization type for each category
  lins    :: Map.Map CId LinFun  -- ^ a linearization function for each function
} deriving (Show)

-- | Abstract function type
-- data Type = Type [CId] CId
--   deriving (Show)

-- -- | Linearisation type
-- data LinType =
--     StrType
--   | IxType Int
--   | ProductType [LinType]
--   deriving (Show)

-- | Linearisation function
data LinFun =
  -- Additions
    Error String -- ^ a runtime error, should probably not be supported at all
  | Bind -- ^ join adjacent tokens
  | Space -- ^ space between adjacent tokens
  | Capit -- ^ capitalise next character
  | AllCapit -- ^ capitalise next word
  | Pre [([Text], LinFun)] LinFun
  | Missing CId -- ^ missing definition (inserted at runtime)

  -- From original definition in paper
  | Empty
  | Token Text
  | Concat LinFun LinFun
  | Ix Int
  | Tuple [LinFun]
  | Projection LinFun LinFun
  | Argument Int

  -- For reducing LPGF file when stored
  | PreIx [(Int, LinFun)] LinFun -- ^ index into `toks` map (must apply read to convert to list)
  | TokenIx Int -- ^ index into `toks` map

  deriving (Show, Read)

instance Binary LPGF where
  put lpgf = do
    put (absname lpgf)
    put (abstract lpgf)
    put (concretes lpgf)
  get = do
    an <- get
    abs <- get
    concs <- get
    return $ LPGF {
      absname = an,
      abstract = abs,
      concretes = concs
    }

instance Binary Abstract where
  put abs = return ()
  get = return $ Abstract {}

instance Binary Concrete where
  put concr = do
    put (toks concr)
    put (lins concr)
  get = do
    ts <- get
    ls <- get
    return $ Concrete {
      toks = ts,
      lins = ls
    }

instance Binary LinFun where
  put = \case
    Error e          -> putWord8 0 >> put e
    Bind             -> putWord8 1
    Space            -> putWord8 2
    Capit            -> putWord8 3
    AllCapit         -> putWord8 4
    Pre ps d         -> putWord8 5 >> put (ps,d)
    Missing f        -> putWord8 13 >> put f

    Empty            -> putWord8 6
    Token t          -> putWord8 7 >> put t
    Concat l1 l2     -> putWord8 8 >> put (l1,l2)
    Ix i             -> putWord8 9 >> put i
    Tuple ls         -> putWord8 10 >> put ls
    Projection l1 l2 -> putWord8 11 >> put (l1,l2)
    Argument i       -> putWord8 12 >> put i

    PreIx ps d       -> putWord8 15 >> put (ps,d)
    TokenIx i        -> putWord8 14 >> put i

  get = do
    tag <- getWord8
    case tag of
      0  -> liftM  Error get
      1  -> return Bind
      2  -> return Space
      3  -> return Capit
      4  -> return AllCapit
      5  -> liftM2 Pre get get
      13 -> liftM  Missing get

      6  -> return Empty
      7  -> liftM  Token get
      8  -> liftM2 Concat get get
      9  -> liftM  Ix get
      10 -> liftM  Tuple get
      11 -> liftM2 Projection get get
      12 -> liftM  Argument get

      15 -> liftM2 PreIx get get
      14 -> liftM  TokenIx get
      _  -> fail "Failed to decode LPGF binary format"

instance Binary Text where
  put = put . TE.encodeUtf8
  get = liftM TE.decodeUtf8 get

abstractName :: LPGF -> CId
abstractName = absname

encodeFile :: FilePath -> LPGF -> IO ()
encodeFile = Data.Binary.encodeFile

readLPGF :: FilePath -> IO LPGF
readLPGF = Data.Binary.decodeFile

-- | Main linearize function, to 'String'
linearize :: LPGF -> Language -> Expr -> String
linearize lpgf lang expr = T.unpack $ linearizeText lpgf lang expr

-- | Main linearize function, to 'Data.Text.Text'
linearizeText :: LPGF -> Language -> Expr -> Text
linearizeText lpgf lang =
  case Map.lookup lang (concretes lpgf) of
    Just concr -> linearizeConcreteText concr
    Nothing -> error $ printf "Unknown language: %s" (showCId lang)

-- | Language-specific linearize function, to 'String'
linearizeConcrete :: Concrete -> Expr -> String
linearizeConcrete concr expr = T.unpack $ linearizeConcreteText concr expr

-- | Language-specific linearize function, to 'Data.Text.Text'
linearizeConcreteText :: Concrete -> Expr -> Text
linearizeConcreteText concr expr = lin2string $ lin (expr2tree expr)
  where
    lin :: Tree -> LinFun
    lin = \case
      Fun f as ->
        case Map.lookup f (lins concr) of
          Just t -> eval cxt t
            where cxt = Context { cxToks = toks concr, cxArgs = map lin as }
          _ -> Missing f
      Lit l -> Tuple [Token (T.pack s)]
        where
          s = case l of
            LStr s -> s
            LInt i -> show i
            LFlt f -> showFFloat (Just 6) f ""
      x -> error $ printf "Cannot lin: %s" (prTree x)

-- -- | Run a computation and catch any exception/errors.
-- -- Ideally this library should never throw exceptions, but we're still in development...
-- try :: a -> IO (Either String a)
-- try comp = do
--   let f = Right <$> EX.evaluate comp
--   EX.catch f (\(e :: EX.SomeException) -> return $ Left (show e))

-- | Evaluation context
data Context = Context {
  cxArgs :: [LinFun], -- ^  is a sequence of terms
  cxToks :: IntMap.IntMap Text -- ^ token map
}

-- | Operational semantics
eval :: Context -> LinFun -> LinFun
eval cxt t = case t of
  Error err -> error err
  Pre pts df -> Pre pts' df'
    where
      pts' = [(pfxs, eval cxt t) | (pfxs, t) <- pts]
      df' = eval cxt df

  Concat s t -> Concat v w
    where
      v = eval cxt s
      w = eval cxt t
  Tuple ts -> Tuple vs
    where vs = map (eval cxt) ts
  Projection t u ->
    case (eval cxt t, eval cxt u) of
      (Missing f, _) -> Missing f
      (_, Missing f) -> Missing f
      (Tuple vs, Ix i) -> vs !! (i-1)
      (t', tv@(Tuple _)) -> eval cxt $ foldl Projection t' (flattenTuple tv)
      (t',u') -> error $ printf "Incompatible projection:\n- %s\n⇓ %s\n- %s\n⇓ %s" (show t) (show t') (show u) (show u')
  Argument i -> cxArgs cxt !! (i-1)

  PreIx pts df -> Pre pts' df'
    where
      pts' = [(pfxs, eval cxt t) | (ix, t) <- pts, let pfxs = maybe [] (read . T.unpack) $ IntMap.lookup ix (cxToks cxt)]
      df' = eval cxt df
  TokenIx i -> maybe Empty Token $ IntMap.lookup i (cxToks cxt)

  _ -> t

flattenTuple :: LinFun -> [LinFun]
flattenTuple = \case
  Tuple vs -> concatMap flattenTuple vs
  lf -> [lf]

-- | Turn concrete syntax terms into an actual string.
-- This is done in two passes, first to flatten concats & evaluate pre's, then to
-- apply BIND and other predefs.
lin2string :: LinFun -> Text
lin2string lf = T.unwords $ join $ flatten [lf]
  where
    -- Process bind et al into final token list
    join :: [Either LinFun Text] -> [Text]
    join elt = case elt of
      Right tok:Left Bind:ls ->
        case join ls of
          next:ls' -> tok `T.append` next : ls'
          _ -> []
      Right tok:ls -> tok : join ls
      Left Space:ls -> join ls
      Left Capit:ls ->
        case join ls of
          next:ls' -> T.toUpper (T.take 1 next) `T.append` T.drop 1 next : ls'
          _ -> []
      Left AllCapit:ls ->
        case join ls of
          next:ls' -> T.toUpper next : ls'
          _ -> []
      Left (Missing cid):ls -> join (Right (T.pack (printf "[%s]" (show cid))) : ls)
      [] -> []
      x -> error $ printf "Unhandled term in lin2string: %s" (show x)

    -- Process concats, tuples, pre into flat list
    flatten :: [LinFun] -> [Either LinFun Text]
    flatten [] = []
    flatten (l:ls) = case l of
      Empty -> flatten ls
      Token "" -> flatten ls
      Token tok -> Right tok : flatten ls
      Concat l1 l2 -> flatten (l1 : l2 : ls)
      Tuple [l] -> flatten (l:ls)
      Tuple (l:_) -> flatten (l:ls) -- unselected table, just choose first option (see e.g. FoodsJpn)
      Pre pts df ->
        let
          f = flatten ls
          ch = case dropWhile isLeft f of
            Right next:_ ->
              let matches = [ l | (pfxs, l) <- pts, any (`T.isPrefixOf` next) pfxs ]
              in  if null matches then df else head matches
            _ -> df
        in flatten (ch:ls)
      x -> Left x : flatten ls

-- | List indexing with more verbose error messages
(!!) :: (Show a) => [a] -> Int -> a
(!!) xs i
  | i < 0 = error $ printf "!!: index %d too small for list: %s" i (show xs)
  | i > length xs - 1 = error $ printf "!!: index %d too large for list: %s" i (show xs)
  | otherwise = xs Prelude.!! i

isIx :: LinFun -> Bool
isIx (Ix _) = True
isIx _ = False

-- | Helper for building concat trees
mkConcat :: [LinFun] -> LinFun
mkConcat [] = Empty
mkConcat [x] = x
mkConcat xs = foldl1 Concat xs

-- | Helper for unfolding concat trees
unConcat :: LinFun -> [LinFun]
unConcat (Concat l1 l2) = concatMap unConcat [l1, l2]
unConcat lf = [lf]

------------------------------------------------------------------------------
-- Pretty-printing

type Doc = CMW.Writer [String] ()

render :: Doc -> String
render = unlines . CMW.execWriter

class PP a where
  pp :: a -> Doc

instance PP LPGF where
  pp (LPGF _ _ cncs) = mapM_ pp cncs

instance PP Concrete where
  pp (Concrete toks lins) = do
    forM_ (IntMap.toList toks) $ \(i,tok) ->
      CMW.tell [show i ++ " " ++ T.unpack tok]
    CMW.tell [""]
    forM_ (Map.toList lins) $ \(cid,lin) -> do
      CMW.tell ["# " ++ showCId cid]
      pp lin
      CMW.tell [""]

instance PP LinFun where
  pp = pp' 0
    where
      pp' n = \case
        Pre ps d -> do
          p "Pre"
          CMW.tell [ replicate (2*(n+1)) ' ' ++ show p | p <- ps ]
          pp' (n+1) d

        c@(Concat l1 l2) -> do
          let ts = unConcat c
          if any isDeep ts
          then do
            p "Concat"
            mapM_ (pp' (n+1)) ts
          else
            p $ "Concat " ++ show ts
        Tuple ls | any isDeep ls -> do
          p "Tuple"
          mapM_ (pp' (n+1)) ls
        Projection l1 l2 | isDeep l1 || isDeep l2 -> do
          p "Projection"
          pp' (n+1) l1
          pp' (n+1) l2
        t -> p $ show t
        where
          p :: String -> Doc
          p t = CMW.tell [ replicate (2*n) ' ' ++ t ]

      isDeep = not . isTerm
      isTerm = \case
        Pre _ _ -> False
        Concat _ _ -> False
        Tuple _ -> False
        Projection _ _ -> False
        _ -> True
