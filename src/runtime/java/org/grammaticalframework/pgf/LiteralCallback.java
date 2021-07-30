package org.grammaticalframework.pgf;

import java.util.Iterator;

public interface LiteralCallback {
	public CallbackResult match(String ann, int start_offset);

	public Iterator<TokenProb> predict(String ann, String prefix);

	public static class CallbackResult {
		private ExprProb ep;
		private int offset;
		
		public CallbackResult(ExprProb ep, int offset) {
			this.ep     = ep;
			this.offset = offset;
		}
		
		public ExprProb getExprProb() {
			return ep;
		}
		
		public int getOffset() {
			return offset;
		}
	}
}
